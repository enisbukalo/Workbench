/**
 * @file llamaServerProcessWindows.cpp
 * @brief Windows-specific implementation for launching llama-server.
 *
 * Uses CreateProcessA() to spawn the llama-server process.
 * Logging is handled by llama-server via --log-file flag.
 */

#include "configManager.h"
#include "llamaServerProcess.h"
#include "modelInfoMonitor.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <iomanip>
#include <json.hpp>
#include <mutex>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

class LlamaServerProcess::Impl
{
  public:
	Impl() : processHandle_(nullptr), jobHandle_(nullptr), httpClient_()
	{
		// Create a job object so child processes are killed when we exit.
		// JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE ensures that if the parent
		// process dies for any reason, the OS terminates the child.
		jobHandle_ = CreateJobObjectA(nullptr, nullptr);
		if (jobHandle_) {
			JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {};
			jeli.BasicLimitInformation.LimitFlags =
				JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
			SetInformationJobObject(jobHandle_,
									JobObjectExtendedLimitInformation,
									&jeli,
									sizeof(jeli));
		}
	}

	~Impl()
	{
		if (running_.load(std::memory_order_acquire)) {
			terminate();
		}
		if (jobHandle_) {
			CloseHandle(jobHandle_);
			jobHandle_ = nullptr;
		}
	}

	bool launch(const std::string &modelPath,
				const Config::ServerSettings &server)
	{
		// Build command arguments (includes --log-file)
		auto args = buildCommandArgs(modelPath, server);

		spdlog::debug("Building llama-server command");

		spdlog::info("Starting llama-server with model: '{}'", modelPath);

		// Use executable path from server settings
		std::string exePath = server.executablePath;
		if (exePath.empty()) {
			spdlog::error("Cannot start llama-server: executablePath is not set "
						  "in config");
			return false;
		}
		std::string argsOnly = buildCommandLine(args);
		spdlog::info("llama-server command: {} {}", exePath, argsOnly);

		// Setup for hidden window
		STARTUPINFOA si = {};
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_HIDE;

		PROCESS_INFORMATION pi = {};

		std::lock_guard<std::mutex> lock(stateMutex_);

		BOOL success = CreateProcessA(exePath.c_str(),	// lpApplicationName
									  argsOnly.data(),	// lpCommandLine
									  nullptr,			// lpProcessAttributes
									  nullptr,			// lpThreadAttributes
									  FALSE,			// bInheritHandles
									  CREATE_NO_WINDOW, // dwCreationFlags
									  nullptr,			// lpEnvironment
									  nullptr,			// lpCurrentDirectory
									  &si,				// lpStartupInfo
									  &pi				// lpProcessInformation
		);

		if (!success) {
			DWORD error = GetLastError();
			spdlog::error(
				"Failed to start llama-server: CreateProcessA error {}",
				error);
			return false;
		}

		spdlog::info("llama-server started (PID: {})", pi.dwProcessId);

		// Close thread handle - we only need process handle
		CloseHandle(pi.hThread);

		// Assign to job object so the child is killed if we crash/exit
		if (jobHandle_) {
			if (!AssignProcessToJobObject(jobHandle_, pi.hProcess)) {
				spdlog::warn("Failed to assign llama-server to job object: {}",
							 GetLastError());
			}
		}

		processHandle_ = pi.hProcess;
		running_.store(true, std::memory_order_release);
		return true;
	}

	bool terminate()
	{
		std::lock_guard<std::mutex> lock(stateMutex_);
		if (!running_.load(std::memory_order_acquire) ||
			processHandle_ == nullptr) {
			return false;
		}

		spdlog::info("Terminating llama-server...");

		BOOL result = TerminateProcess(processHandle_, 1);
		if (result) {
			WaitForSingleObject(processHandle_, INFINITE);
		}

		CloseHandle(processHandle_);
		processHandle_ = nullptr;
		running_.store(false, std::memory_order_release);

		if (result) {
			spdlog::info("llama-server terminated");
		}

		return result != FALSE;
	}

	bool isRunning()
	{
		if (!running_.load(std::memory_order_acquire)) {
			return false;
		}
		// Lock so the CloseHandle below can never race a concurrent
		// terminate() into a double-close of the same handle.
		std::lock_guard<std::mutex> lock(stateMutex_);
		if (processHandle_ == nullptr) {
			return false;
		}

		DWORD exitCode;
		if (GetExitCodeProcess(processHandle_, &exitCode)) {
			if (exitCode == STILL_ACTIVE) {
				return true;
			}
			// Process has exited — clean up stale state
			CloseHandle(processHandle_);
			processHandle_ = nullptr;
			running_.store(false, std::memory_order_release);
			return false;
		}
		return false;
	}

	intptr_t getHandle() const
	{
		std::lock_guard<std::mutex> lock(stateMutex_);
		return reinterpret_cast<intptr_t>(processHandle_);
	}

	bool isServerHealthy()
	{
		if (!running_.load(std::memory_order_acquire))
			return false;
		const auto cfgServer = ConfigManager::instance().getServerSettings();
		std::string url = "http://" + cfgServer.connectHost() + ":" +
						  std::to_string(cfgServer.port) + "/health";
		auto [success, response] = httpClient_.get(url);
		return success && response.find("ok") != std::string::npos;
	}

	std::string getSlotStatus(const std::string &modelName)
	{
		if (!isServerHealthy() || modelName.empty())
			return "";
		const auto cfgServer = ConfigManager::instance().getServerSettings();
		std::string url = "http://" + cfgServer.connectHost() + ":" +
						  std::to_string(cfgServer.port) +
						  "/slots?model=" + HttpClient::urlEncode(modelName);
		auto [success, response] = httpClient_.get(url);
		if (!success) {
			spdlog::debug("getSlotStatus failed: {}", response);
			return "";
		}
		return response;
	}

	LlamaServerProcess::WorkerState
	probeModelWorker(const std::string &modelName)
	{
		using WorkerState = LlamaServerProcess::WorkerState;
		if (modelName.empty())
			return WorkerState::BUSY; // neutral: nothing to probe
		// Proxied GET to the worker via the router. A live worker answers 200;
		// a crashed/unreachable worker yields a refused connection (#18912); a
		// busy worker times out. Classify so only a real crash counts.
		const auto cfgServer = ConfigManager::instance().getServerSettings();
		std::string url = "http://" + cfgServer.connectHost() + ":" +
						  std::to_string(cfgServer.port) +
						  "/slots?model=" + HttpClient::urlEncode(modelName);
		auto [result, response] = httpClient_.getWithResult(url);
		auto state = LlamaServerProcess::classifyProbeResponse(result, response);
		if (state == WorkerState::DEAD) {
			spdlog::debug("probeModelWorker: worker unreachable: {}", response);
		} else if (state == WorkerState::BUSY) {
			// Fail-safe: never restart on an ambiguous/slow probe.
			spdlog::debug("probeModelWorker: busy/ambiguous: {}", response);
		}
		return state;
	}

	bool isModelLoaded()
	{
		if (!isServerHealthy())
			return false;
		const auto cfgServer = ConfigManager::instance().getServerSettings();
		std::string url = "http://" + cfgServer.connectHost() + ":" +
						  std::to_string(cfgServer.port) + "/models";
		auto [success, response] = httpClient_.get(url);
		if (!success) {
			spdlog::debug("isModelLoaded failed: {}", response);
			return false;
		}
		// Check if any model has status "loaded"
		return response.find("\"loaded\"") != std::string::npos;
	}

	std::string getLoadedModelPath()
	{
		if (!isServerHealthy())
			return "";
		const auto cfgServer = ConfigManager::instance().getServerSettings();
		std::string url = "http://" + cfgServer.connectHost() + ":" +
						  std::to_string(cfgServer.port) + "/models";
		auto [success, response] = httpClient_.get(url);
		if (!success) {
			spdlog::debug("getLoadedModelPath failed: {}", response);
			return "";
		}
		std::string modelId = ModelInfoMonitor::parseLoadedModelId(response);
		// Log the /models body only when the loaded model changes, not every
		// poll. getLoadedModelPath runs on both the poll thread and the UI
		// thread (via unloadModel), so the last-seen marker is a lock-free
		// std::atomic hash: a stale read at worst prints one extra/fewer debug
		// line, never a torn std::string read.
		std::size_t idHash = std::hash<std::string>{}(modelId);
		if (lastLoggedModelIdHash_.exchange(idHash, std::memory_order_relaxed) !=
			idHash) {
			spdlog::debug("/models response: {}", response);
		}
		return modelId;
	}

	std::vector<std::string> getLoadedModelIds()
	{
		if (!isServerHealthy())
			return {};
		const auto cfgServer = ConfigManager::instance().getServerSettings();
		std::string url = "http://" + cfgServer.connectHost() + ":" +
						  std::to_string(cfgServer.port) + "/models";
		auto [success, response] = httpClient_.get(url);
		if (!success) {
			spdlog::debug("getLoadedModelIds failed: {}", response);
			return {};
		}
		return ModelInfoMonitor::parseLoadedModelIds(response);
	}

	bool unloadModelByName(const std::string &name)
	{
		const auto cfgServer = ConfigManager::instance().getServerSettings();
		std::string url = "http://" + cfgServer.connectHost() + ":" +
						  std::to_string(cfgServer.port) + "/models/unload";
		nlohmann::json bodyJson;
		bodyJson["model"] = name;
		auto [success, response] = httpClient_.post(url, bodyJson.dump());
		if (success) {
			spdlog::info("Model '{}' unloaded successfully", name);
		} else {
			spdlog::error("Failed to unload model '{}': {}", name, response);
		}
		return success;
	}

	bool unloadModel()
	{
		if (!isServerHealthy())
			return false;
		// Get the currently loaded model name to send in the unload request
		std::string loadedModel = getLoadedModelPath();
		if (loadedModel.empty()) {
			spdlog::warn("No model loaded, nothing to unload");
			return false;
		}
		return unloadModelByName(loadedModel);
	}

	bool unloadModel(const std::string &name)
	{
		if (!isServerHealthy())
			return false;
		if (name.empty())
			return false;
		return unloadModelByName(name);
	}

	bool unloadAllModels()
	{
		if (!isServerHealthy())
			return false;
		const auto cfgServer = ConfigManager::instance().getServerSettings();
		std::string url = "http://" + cfgServer.connectHost() + ":" +
						  std::to_string(cfgServer.port) + "/models";
		auto [success, response] = httpClient_.get(url);
		if (!success) {
			spdlog::error("unloadAllModels: failed to query /models: {}",
						  response);
			return false;
		}
		auto ids = ModelInfoMonitor::parseLoadedModelIds(response);
		if (ids.empty()) {
			spdlog::info("unloadAllModels: no models loaded, nothing to do");
			return true;
		}
		bool allOk = true;
		for (const auto &id : ids) {
			spdlog::info("unloadAllModels: unloading '{}'", id);
			allOk = unloadModelByName(id) && allOk;
		}
		return allOk;
	}

	bool loadModel(const std::string &modelIdentifier)
	{
		if (!isServerHealthy())
			return false;
		// modelIdentifier is now the section name from models.ini (e.g.,
		// "orchestrator") Use it directly for the API call
		const auto cfgServer = ConfigManager::instance().getServerSettings();
		std::string url = "http://" + cfgServer.connectHost() + ":" +
						  std::to_string(cfgServer.port) + "/models/load";
		nlohmann::json bodyJson;
		bodyJson["model"] = modelIdentifier;
		auto [success, response] = httpClient_.post(url, bodyJson.dump());
		if (success) {
			spdlog::info("Model loaded: {}", modelIdentifier);
		} else {
			spdlog::error("Failed to load model: {}", response);
		}
		return success;
	}

  private:
	std::string buildCommandLine(const std::vector<std::string> &args)
	{
		// quoteWindowsArg applies the CommandLineToArgvW re-parsing rules;
		// naive "\"" + arg + "\"" broke on args with trailing backslashes
		// (every Windows directory path) or embedded quotes.
		std::string cmdLine;
		for (const auto &arg : args) {
			cmdLine += LlamaServerProcess::quoteWindowsArg(arg) + " ";
		}
		if (!cmdLine.empty() && cmdLine.back() == ' ') {
			cmdLine.pop_back();
		}
		return cmdLine;
	}

	// processHandle_ transitions are guarded by stateMutex_; running_ is
	// additionally atomic so hot paths (isServerHealthy on the poll thread)
	// can read it without taking the lock.
	HANDLE processHandle_;
	HANDLE jobHandle_;
	std::atomic<bool> running_{ false };
	mutable std::mutex stateMutex_;
	HttpClient httpClient_;
	// Hash of the last loaded-model id whose /models body was logged. Atomic
	// because getLoadedModelPath is reached from both the poll and UI threads;
	// only gates a debug line, so relaxed ordering is sufficient.
	std::atomic<std::size_t> lastLoggedModelIdHash_{ 0 };
};

LlamaServerProcess::LlamaServerProcess() : m_impl(std::make_unique<Impl>())
{
}

LlamaServerProcess::~LlamaServerProcess() = default;

bool LlamaServerProcess::launch(const std::string &modelPath,
								const Config::ServerSettings &server)
{
	// Preserve the existing worker log before llama-server truncates it on
	// launch, so a restart never destroys the evidence of why it restarted.
	backupServerLog();
	return m_impl->launch(modelPath, server);
}

bool LlamaServerProcess::terminate()
{
	return m_impl->terminate();
}

bool LlamaServerProcess::isRunning() const
{
	return m_impl->isRunning();
}

intptr_t LlamaServerProcess::getHandle() const
{
	return m_impl->getHandle();
}

void LlamaServerProcess::setOutputCallback(
	std::function<void(const std::string &)> callback)
{
	// Not used - output goes to log file via --log-file
	(void)callback;
}

LlamaServerProcess &LlamaServerProcess::instance()
{
	static LlamaServerProcess process;
	return process;
}

std::string LlamaServerProcess::getLogPath()
{
	return ConfigManager::getLogsDir() + "/llama-server.log";
}

bool LlamaServerProcess::isModelLoaded()
{
	return m_impl->isModelLoaded();
}

std::string LlamaServerProcess::getLoadedModelPath()
{
	return m_impl->getLoadedModelPath();
}

std::vector<std::string> LlamaServerProcess::getLoadedModelIds()
{
	return m_impl->getLoadedModelIds();
}

bool LlamaServerProcess::unloadModel()
{
	return m_impl->unloadModel();
}

bool LlamaServerProcess::unloadModel(const std::string &modelName)
{
	return m_impl->unloadModel(modelName);
}

bool LlamaServerProcess::unloadAllModels()
{
	return m_impl->unloadAllModels();
}

bool LlamaServerProcess::loadModel(const std::string &modelPath)
{
	return m_impl->loadModel(modelPath);
}

bool LlamaServerProcess::isServerHealthy()
{
	return m_impl->isServerHealthy();
}

std::string LlamaServerProcess::getSlotStatus(const std::string &modelName)
{
	return m_impl->getSlotStatus(modelName);
}

LlamaServerProcess::WorkerState
LlamaServerProcess::probeModelWorker(const std::string &modelName)
{
	return m_impl->probeModelWorker(modelName);
}