#include "llamaServerProcess.h"
#include "modelsIni.h"
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <spdlog/spdlog.h>
#include <sstream>
#include <vector>

namespace {

/**
 * @brief Write the API key to a private file and return its path.
 *
 * Command lines are readable by every local process (ps on Linux, Task
 * Manager / WMI on Windows), so passing the key inline via --api-key would
 * leak it. The key is written to <configDir>/llama-api-key.txt instead and
 * handed to llama-server via --api-key-file. On POSIX the file is restricted
 * to owner read/write; on Windows the user-profile directory ACL applies.
 *
 * @param apiKey The secret to persist.
 * @return Path to the key file, or "" on write failure.
 */
std::string writeApiKeyFile(const std::string &apiKey)
{
	std::string path = ConfigManager::getConfigDir() + "/llama-api-key.txt";
	try {
		// First launch may precede ConfigManager::load(); make sure the
		// directory exists before opening the file.
		std::filesystem::create_directories(ConfigManager::getConfigDir());
		std::ofstream out(path, std::ios::trunc);
		if (!out.is_open()) {
			spdlog::error("Failed to open api-key file for writing: {}", path);
			return "";
		}
		out << apiKey;
		out.close();
		std::error_code ec;
		std::filesystem::permissions(path,
									 std::filesystem::perms::owner_read |
										 std::filesystem::perms::owner_write,
									 std::filesystem::perm_options::replace,
									 ec);
		if (ec) {
			// Non-fatal: on Windows POSIX perms don't apply.
			spdlog::debug("api-key file permissions not applied: {}",
						  ec.message());
		}
		return path;
	} catch (const std::exception &e) {
		spdlog::error("Failed to write api-key file: {}", e.what());
		return "";
	}
}

} // namespace

void LlamaServerProcess::backupServerLog()
{
	try {
		std::filesystem::path logPath(getLogPath());
		// Nothing to preserve if the log is missing or empty (fresh boot, or a
		// server that never wrote anything).
		std::error_code ec;
		if (!std::filesystem::exists(logPath, ec) ||
			std::filesystem::file_size(logPath, ec) == 0) {
			return;
		}

		// Timestamped sibling. The "_workbench.log" suffix is deliberate: it is
		// what cleanupOldLogs() matches, so these backups age out with the rest.
		auto now = std::chrono::system_clock::now();
		auto time = std::chrono::system_clock::to_time_t(now);
		std::stringstream ss;
		ss << std::put_time(std::localtime(&time), "%Y%m%d_%H%M%S")
		   << "_llama-server_workbench.log";
		std::filesystem::path backupPath = logPath.parent_path() / ss.str();

		std::filesystem::copy_file(
			logPath,
			backupPath,
			std::filesystem::copy_options::overwrite_existing,
			ec);
		if (ec) {
			spdlog::warn("Failed to back up llama-server log: {}", ec.message());
		} else {
			spdlog::info("Backed up llama-server log to {}",
						 backupPath.filename().string());
		}
	} catch (const std::exception &e) {
		spdlog::warn("Failed to back up llama-server log: {}", e.what());
	}
}

std::vector<std::string>
LlamaServerProcess::buildCommandArgs(const std::string &modelPath,
									 const Config::ServerSettings &server)
{
	// Router mode loads models on demand via the API, not from a launch-time
	// model path. modelPath is retained in the signature for interface
	// compatibility but is unused here.
	(void)modelPath;

	std::vector<std::string> args;

	// Base command
	args.push_back("llama-server");

	// Router mode: use --models-preset to point to models.ini
	// The INI file defines all available models.
	//
	// Per-model load (ctx size, ngl, cache types, ...) and inference (temp,
	// top-k, ...) settings are NOT passed on the launch command line: in router
	// mode the model is loaded on demand via POST /models/load, which reads the
	// per-model settings from models.ini. Forcing them as global CLI flags here
	// would only shadow/override those per-model values (llama.cpp #20851), so
	// they are intentionally omitted.
	std::string iniPath = ModelsIni::instance().getPath();
	if (!iniPath.empty() && std::filesystem::exists(iniPath)) {
		args.push_back("--models-preset");
		args.push_back(iniPath);
	} else if (!iniPath.empty()) {
		spdlog::warn("models.ini path set but file does not exist: {}", iniPath);
	}

	// Auto-load policy. Default is --no-models-autoload: Workbench loads models
	// on demand via POST /models/load when the user clicks LOAD. Without this,
	// the router preloads the first/last models.ini entry on boot, which after a
	// crash-restart brings the just-crashed model straight back (#85). Only when
	// the user explicitly opts in (modelsAutoload=true) do we emit the positive
	// form, accepting that #85 risk.
	args.push_back(server.modelsAutoload ? "--models-autoload"
										 : "--no-models-autoload");

	// Router slot cap.
	args.push_back("--models-max");
	args.push_back(std::to_string(server.modelsMax));

	// === Server Settings ===
	// Network
	if (!server.host.empty()) {
		args.push_back("--host");
		args.push_back(server.host);
	}
	args.push_back("--port");
	args.push_back(std::to_string(server.port));

	// Authentication.
	// Security: the key always travels via --api-key-file. An inline
	// --api-key would appear in the process command line, which any local
	// process can read. An explicit apiKeyFile wins; otherwise a configured
	// apiKey is written to a private file first. Only if that write fails is
	// the inline flag used as a last resort (an unprotected server would be
	// worse than a visible key), with a warning.
	if (!server.apiKeyFile.empty()) {
		args.push_back("--api-key-file");
		args.push_back(server.apiKeyFile);
	} else if (!server.apiKey.empty()) {
		std::string keyPath = writeApiKeyFile(server.apiKey);
		if (!keyPath.empty()) {
			args.push_back("--api-key-file");
			args.push_back(keyPath);
		} else {
			spdlog::warn("api-key file write failed; passing key on the "
						 "command line (visible to local processes)");
			args.push_back("--api-key");
			args.push_back(server.apiKey);
		}
	}
	if (server.timeout > 0) {
		args.push_back("--timeout");
		args.push_back(std::to_string(server.timeout));
	}
	if (server.threadsHttp > 0) {
		args.push_back("--threads-http");
		args.push_back(std::to_string(server.threadsHttp));
	}
	if (server.reusePort) {
		args.push_back("--reuse-port");
	}

	// SSL/TLS
	if (!server.sslKeyFile.empty()) {
		args.push_back("--ssl-key-file");
		args.push_back(server.sslKeyFile);
	}
	if (!server.sslCertFile.empty()) {
		args.push_back("--ssl-cert-file");
		args.push_back(server.sslCertFile);
	}

	// Static file serving
	if (!server.path.empty()) {
		args.push_back("--path");
		args.push_back(server.path);
	}
	if (!server.apiPrefix.empty()) {
		args.push_back("--api-prefix");
		args.push_back(server.apiPrefix);
	}
	if (!server.mediaPath.empty()) {
		args.push_back("--media-path");
		args.push_back(server.mediaPath);
	}

	// Server behavior
	if (!server.alias.empty()) {
		args.push_back("--alias");
		args.push_back(server.alias);
	}
	args.push_back(server.ui ? "--ui" : "--no-ui");
	if (!server.uiConfig.empty()) {
		args.push_back("--ui-config");
		args.push_back(server.uiConfig);
	}
	if (!server.uiConfigFile.empty()) {
		args.push_back("--ui-config-file");
		args.push_back(server.uiConfigFile);
	}
	args.push_back(server.uiMcpProxy ? "--ui-mcp-proxy" : "--no-ui-mcp-proxy");
	if (!server.tools.empty()) {
		args.push_back("--tools");
		args.push_back(server.tools);
	}
	if (server.embedding) {
		args.push_back("--embedding");
	}
	if (server.reranking) {
		args.push_back("--rerank");
	}
	args.push_back(server.contBatching ? "--cont-batching"
									   : "--no-cont-batching");
	args.push_back(server.cachePrompt ? "--cache-prompt" : "--no-cache-prompt");
	if (server.cacheReuse > 0) {
		args.push_back("--cache-reuse");
		args.push_back(std::to_string(server.cacheReuse));
	}
	// Always emit: 0 (disable) is a meaningful value, so no >0 guard here.
	args.push_back("--cache-ram");
	args.push_back(std::to_string(server.cacheRam));
	// Always emit: 0 (disable) is a meaningful value, so no >0 guard here.
	args.push_back("--ctx-checkpoints");
	args.push_back(std::to_string(server.ctxCheckpoints));
	// Distinct from --ctx-checkpoints: minimum token spacing between
	// checkpoints. Renamed from --checkpoint-every-n-tokens in llama.cpp b9842.
	args.push_back("--checkpoint-min-step");
	args.push_back(std::to_string(server.checkpointMinStep));
	args.push_back(server.cacheIdleSlots ? "--cache-idle-slots"
										 : "--no-cache-idle-slots");
	if (!server.pooling.empty()) {
		args.push_back("--pooling");
		args.push_back(server.pooling);
	}
	if (server.embdNormalize != -1) {
		args.push_back("--embd-normalize");
		args.push_back(std::to_string(server.embdNormalize));
	}
	if (!server.reasoning.empty()) {
		args.push_back("--reasoning");
		args.push_back(server.reasoning);
	}
	if (server.reasoningBudget != -1) {
		args.push_back("--reasoning-budget");
		args.push_back(std::to_string(server.reasoningBudget));
	}
	args.push_back(server.contextShift ? "--context-shift"
									   : "--no-context-shift");
	args.push_back(server.warmup ? "--warmup" : "--no-warmup");
	args.push_back(server.jinja ? "--jinja" : "--no-jinja");
	args.push_back(server.prefillAssistant ? "--prefill-assistant"
										   : "--no-prefill-assistant");
	if (server.slotPromptSimilarity >= 0.0 &&
		server.slotPromptSimilarity <= 1.0) {
		args.push_back("--slot-prompt-similarity");
		args.push_back(std::to_string(server.slotPromptSimilarity));
	}
	if (server.sleepIdleSeconds >= 0) {
		args.push_back("--sleep-idle-seconds");
		args.push_back(std::to_string(server.sleepIdleSeconds));
	}

	// Endpoints
	if (server.metrics) {
		args.push_back("--metrics");
	}
	if (server.props) {
		args.push_back("--props");
	}
	args.push_back(server.slots ? "--slots" : "--no-slots");
	if (!server.slotSavePath.empty()) {
		args.push_back("--slot-save-path");
		args.push_back(server.slotSavePath);
	}

	// Log file - use llama-server's built-in logging with timestamps
	args.push_back("--log-file");
	args.push_back(ConfigManager::getLogsDir() + "/llama-server.log");
	args.push_back("--log-timestamps");
	args.push_back("--log-prefix");
	args.push_back("--log-colors");
	args.push_back("off");

	return args;
}

LlamaServerProcess::WorkerState
LlamaServerProcess::classifyProbeResponse(HttpClient::HttpResult result,
										  const std::string &body)
{
	switch (result) {
	case HttpClient::HttpResult::OK:
		return WorkerState::ALIVE;
	case HttpClient::HttpResult::CONNECT_ERROR:
		// Refused/closed connection: the worker process is gone (#18912).
		return WorkerState::DEAD;
	case HttpClient::HttpResult::TIMEOUT:
		// Reachable but slow (heavy decode) — a stall, never a crash.
		return WorkerState::BUSY;
	case HttpClient::HttpResult::OTHER_ERROR:
	default:
		// Some llama-server builds don't refuse the proxied connection when
		// the worker dies; the router itself answers HTTP 500
		// "proxy error: Could not establish connection". That body is the
		// router saying it cannot reach the worker — a death, not a stall.
		if (body.find("proxy error") != std::string::npos) {
			return WorkerState::DEAD;
		}
		// Fail-safe: anything else ambiguous is BUSY (never restart on it).
		return WorkerState::BUSY;
	}
}

std::string LlamaServerProcess::quoteWindowsArg(const std::string &arg)
{
	// Implements the quoting rules CommandLineToArgvW / the MSVC CRT use to
	// re-parse a command line: backslashes are literal except when they
	// precede a double quote, in which case each one must be doubled and the
	// quote itself escaped. The naive wrap-in-quotes approach breaks on args
	// ending in '\' (e.g. "D:\Models\") because '\"' re-parses as an escaped
	// quote that swallows the rest of the line.
	if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) {
		return arg; // no quoting needed
	}

	std::string out;
	out.reserve(arg.size() + 2);
	out += '"';
	std::size_t i = 0;
	while (i < arg.size()) {
		std::size_t backslashes = 0;
		while (i < arg.size() && arg[i] == '\\') {
			++backslashes;
			++i;
		}
		if (i == arg.size()) {
			// Trailing backslashes: double them so the closing quote stays a
			// delimiter rather than becoming escaped.
			out.append(backslashes * 2, '\\');
			break;
		}
		if (arg[i] == '"') {
			// Backslashes before a quote are escape characters: double them,
			// then escape the quote.
			out.append(backslashes * 2 + 1, '\\');
			out += '"';
		} else {
			out.append(backslashes, '\\');
			out += arg[i];
		}
		++i;
	}
	out += '"';
	return out;
}