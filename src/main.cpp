/**
 * @file main.cpp
 * @brief Application entry point.
 *
 * Minimal main function that delegates all functionality to App::Run().
 * This file serves as the compilation entry point for the Workbench TUI.
 */

#include "app.h"
#include "batchStore.h"
#include "configManager.h"
#include "controlApiServer.h"
#include "llamaServerProcess.h"
#include "modelInfoMonitor.h"
#include "modelStateTracker.h"
#include "modelsIni.h"
#include "systemMonitorRunner.h"
#include "vllmMonitor.h"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
#include <sstream>

/**
 * @brief Cleans up old log files based on retention policy.
 * @param logDir Path to the logs directory.
 * @param retentionDays Number of days to retain log files.
 * @param currentLogFile Name of the current log file (to avoid deleting it).
 */
void cleanupOldLogs(const std::string &logDir,
					int retentionDays,
					const std::string &currentLogFile)
{
	if (retentionDays <= 0) {
		spdlog::debug("Log cleanup disabled (retention: 0)");
		return;
	}

	spdlog::info("Starting log cleanup (retention: {} days)", retentionDays);

	try {
		std::filesystem::path logsPath(logDir);
		if (!std::filesystem::exists(logsPath)) {
			spdlog::debug("No log files to clean up");
			return;
		}

		int deletedCount = 0;

		for (const auto &entry : std::filesystem::directory_iterator(logsPath)) {
			if (!entry.is_regular_file())
				continue;

			std::string filename = entry.path().filename().string();

			// Skip if not a workbench log file
			if (filename.find("_workbench.log") == std::string::npos)
				continue;

			// Skip the current log file (don't delete the one we just created)
			if (filename == currentLogFile)
				continue;

			// Calculate file age using file_clock directly (avoids
			// non-portable epoch conversions between clocks)
			auto fileTime = std::filesystem::last_write_time(entry.path());
			auto fileAge =
				std::filesystem::file_time_type::clock::now() - fileTime;
			double ageHours =
				std::chrono::duration<double, std::ratio<3600>>(fileAge).count();

			if (ageHours >= retentionDays * 24) {
				std::filesystem::remove(entry.path());
				deletedCount++;
				spdlog::debug("Deleted old log: {}", filename);
			}
		}

		if (deletedCount > 0)
			spdlog::info("Cleaned up {} old log file(s)", deletedCount);
		else
			spdlog::debug("No old log files to clean up");
	} catch (const std::exception &e) {
		spdlog::warn("Failed to clean up old logs: {}", e.what());
	}
}

/**
 * @brief Program entry point.
 *
 * Loads user configuration and launches the Workbench terminal UI.
 * The SystemMonitorRunner is started from within App::run() after the
 * screen is created, so it can trigger redraws when monitor data updates.
 *
 * @return 0 on successful execution.
 */
int main()
{
	std::string logFilename; // Store for cleanup function

	try {
		// Create logs directory if it doesn't exist
		std::filesystem::path logsDir = ConfigManager::instance().getLogsDir();
		std::filesystem::create_directories(logsDir);

		// Generate timestamp for log filename
		auto now = std::chrono::system_clock::now();
		auto time = std::chrono::system_clock::to_time_t(now);
		std::stringstream ss;
		ss << std::put_time(std::localtime(&time), "%Y%m%d_%H%M%S");
		logFilename = ss.str() + "_workbench.log";
		std::string logPath = (logsDir / logFilename).string();

		// Create basic file logger with timestamp in filename
		auto fileLogger = spdlog::basic_logger_mt("workbench", logPath, true);

		// Set pattern: [timestamp] [level] message
		fileLogger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%-7l%$] %v");

		// Register as default logger FIRST (before any logging)
		spdlog::set_default_logger(fileLogger);

		// Set default level to debug (capture everything)
		spdlog::set_level(spdlog::level::debug);

		// Flush on trace level (lowest) - covers all levels above
		spdlog::flush_on(spdlog::level::trace);

		spdlog::info("Workbench starting... (log: {})", logPath);
	} catch (const spdlog::spdlog_ex &ex) {
		// Fallback to basic console if file sink fails
		spdlog::set_level(spdlog::level::debug);
		spdlog::warn("Failed to initialize file logging, using default: {}",
					 ex.what());
	}

	// Load configuration from disk
	ConfigManager::instance().load();

	// Initialize models.ini (create if doesn't exist)
	if (!ModelsIni::instance().load()) {
		spdlog::info("Creating default models.ini");
		ModelsIni::instance().createDefault();
	}

	// Load saved model batches (issue #111). A missing file is fine — the store
	// stays empty until the user saves a batch.
	BatchStore::instance().load();

	// Clean up old log files based on retention policy
	cleanupOldLogs(
		ConfigManager::instance().getLogsDir(),
		ConfigManager::instance().getConfigSnapshot().ui.logRetentionDays,
		logFilename);

	// Auto-start llama-server on app launch (router mode, no model loaded yet)
	bool serverStarted = LlamaServerProcess::instance().launch(
		"", // Empty model path = no model initially
		ConfigManager::instance().getServerSettings());

	if (serverStarted) {
		spdlog::info("Llama-server auto-started on app launch");
	} else {
		spdlog::warn("Failed to auto-start llama-server");
	}

	// Start model info monitoring (runs in background during UI).
	// Begin in the force-unloaded state: at boot no model is loaded, and the
	// monitor's /models + /slots polls would otherwise make the router
	// auto-load the last-used model (issue #71). The tracker's skip flag is
	// cleared when the user explicitly loads a model (requestLoad).
	ModelStateTracker::instance().requestUnloadAll();
	ModelInfoMonitor::instance().start();

	// Start vLLM monitoring (runs in background during UI).
	// Polls vLLM server endpoints; disabled when host is empty.
	VllmMonitor::instance().start();

	// Auto-start the inbound control API if the user enabled it. The Settings
	// panel toggles it live; here we honor the persisted choice at launch.
	{
		const auto apiSettings =
			ConfigManager::instance().getConfigSnapshot().api;
		if (apiSettings.apiEnabled) {
			if (ControlApiServer::instance().start(apiSettings))
				spdlog::info("Inbound control API started on {}:{}",
							 apiSettings.apiHost,
							 apiSettings.apiPort);
			else
				spdlog::warn("Failed to start inbound control API on {}:{}",
							 apiSettings.apiHost,
							 apiSettings.apiPort);
		}
	}

	// Run the main application UI loop
	App::run();

	// Stop the inbound control API listener (joins its accept thread).
	ControlApiServer::instance().stop();

	// Clean up the model info monitor before system monitor
	ModelInfoMonitor::instance().stop();

	// Clean up the vLLM monitor
	VllmMonitor::instance().stop();

	// Clean up the system monitor before exit
	SystemMonitorRunner::instance().stop();

	// Terminate llama-server if still running
	LlamaServerProcess::instance().terminate();

	// Flush logs before exit
	spdlog::default_logger()->flush();
	spdlog::info("Workbench exiting.");
	return 0;
}
