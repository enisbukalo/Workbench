#pragma once

#include "ILlamaServerProcess.h"
#include "config.h"
#include "configManager.h"
#include "httpClient.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

/**
 * @class LlamaServerProcess
 * @brief Platform-agnostic interface for launching and managing llama-server.
 *
 * This class uses the pImpl idiom - the actual implementation is in the
 * platform- specific .cpp files (llamaServerProcessLinux.cpp,
 * llamaServerProcessWindows.cpp).
 *
 * Usage:
 *   auto process = std::make_unique<LlamaServerProcess>();
 *   bool success = process->launch(modelPath, loadSettings, inferenceSettings,
 * serverSettings);
 *   // ...
 *   process->terminate();  // On app exit
 *
 * Or use the singleton for global cleanup:
 *   LlamaServerProcess::instance().terminate();
 */
class LlamaServerProcess : public ILlamaServerProcess
{
  public:
	/**
	 * @brief Liveness of a router-mode model worker, as seen by a proxied probe.
	 *
	 * ALIVE — the worker answered (HTTP 200).
	 * BUSY  — the worker is reachable but did not answer in time (slow decode);
	 *         a transient stall, NOT a crash. Must not trigger a restart.
	 * DEAD  — the proxied request was refused/closed: the worker process is gone
	 *         while the router parent stays up and /models still lies "loaded"
	 *         (llama.cpp #18912). Only this state counts toward crash detection.
	 */
	enum class WorkerState
	{
		ALIVE,
		BUSY,
		DEAD
	};

	/**
	 * @brief Get the singleton instance.
	 *
	 * Provides global access to the process handle for cleanup during
	 * application shutdown. Uses Meyers' singleton pattern.
	 *
	 * @return Reference to the singleton instance.
	 */
	static LlamaServerProcess &instance();

	LlamaServerProcess();
	~LlamaServerProcess();

	// Delete copy/move - process handle is not copyable
	LlamaServerProcess(const LlamaServerProcess &) = delete;
	LlamaServerProcess &operator=(const LlamaServerProcess &) = delete;
	LlamaServerProcess(LlamaServerProcess &&) = delete;
	LlamaServerProcess &operator=(LlamaServerProcess &&) = delete;

	/**
	 * @brief Launch llama-server in router mode.
	 *
	 * Load and inference settings are intentionally not passed here: in router
	 * mode models are loaded on demand via the HTTP API, which reads per-model
	 * settings from models.ini. Only server settings shape the launch command.
	 *
	 * @param modelPath Full path to the .gguf model file (unused in router mode;
	 *                  retained for interface compatibility)
	 * @param server Server settings (host, port, etc.)
	 * @return true if launch succeeded, false on failure
	 */
	bool launch(const std::string &modelPath,
				const Config::ServerSettings &server) override;

	/**
	 * @brief Terminate the running llama-server process.
	 *
	 * Linux: SIGTERM, then up to ~5 s grace, then SIGKILL.
	 * Windows: forceful TerminateProcess (no graceful phase).
	 *
	 * @return true if terminated successfully, false on failure
	 */
	bool terminate() override;

	/**
	 * @brief Check if the process is currently running.
	 * @return true if running, false otherwise
	 */
	bool isRunning() const override;

	/**
	 * @brief Get the platform-specific process handle.
	 * @return On Linux: pid_t (process ID)
	 *         On Windows: HANDLE (process handle)
	 */
	intptr_t getHandle() const;

	/**
	 * @brief Build the router-mode command argument vector.
	 *
	 * Emits only `--models-preset` and server flags. Per-model load/inference
	 * settings come from models.ini via the load API, not the command line.
	 *
	 * @param modelPath Path to the model file (unused in router mode)
	 * @param server Server settings
	 * @return Vector of command arguments suitable for execve() or conversion
	 */
	static std::vector<std::string>
	buildCommandArgs(const std::string &modelPath,
					 const Config::ServerSettings &server);

	/**
	 * @brief Get the path to the llama-server log file.
	 * @return The full path to the log file (e.g.,
	 * ~/.workbench/logs/llama-server.log)
	 */
	static std::string getLogPath();

	/**
	 * @brief Quote a single argument for a Windows command line.
	 *
	 * Applies the CommandLineToArgvW re-parsing rules: doubles backslash runs
	 * that precede a double quote (or the closing quote) and escapes embedded
	 * quotes. Args without whitespace or quotes are returned unchanged.
	 * Platform-neutral pure function; exposed for unit testing.
	 *
	 * @param arg Raw argument value.
	 * @return Argument safe to splice into a CreateProcess command line.
	 */
	[[nodiscard]] static std::string quoteWindowsArg(const std::string &arg);

	/**
	 * @brief Classify a proxied /slots probe outcome as a WorkerState.
	 *
	 * Maps the HTTP result of GET /slots?model=NAME (proxied by the router to
	 * the worker) onto worker liveness:
	 * - OK → ALIVE.
	 * - CONNECT_ERROR (refused/closed) → DEAD: the worker process is gone.
	 * - OTHER_ERROR with a body containing "proxy error" → DEAD: some
	 *   llama-server builds don't surface a dead worker as a refused
	 *   connection; the router itself answers HTTP 500
	 *   "proxy error: Could not establish connection" (#18912 variant).
	 * - TIMEOUT or anything else → BUSY (fail-safe: never restart on an
	 *   ambiguous or merely slow probe).
	 *
	 * Pure function shared by both platform Impls; exposed for unit testing.
	 *
	 * @param result HTTP outcome class from HttpClient::getWithResult().
	 * @param body   Response body / error string accompanying @p result.
	 * @return Classified worker liveness.
	 */
	[[nodiscard]] static WorkerState
	classifyProbeResponse(HttpClient::HttpResult result,
						  const std::string &body);

	/**
	 * @brief Back up the current llama-server log before it is overwritten.
	 *
	 * llama-server opens its `--log-file` in truncate mode on every launch, so a
	 * restart wipes the prior worker log — exactly the evidence needed to tell a
	 * real crash from a false positive. This copies the existing
	 * `llama-server.log` to a timestamped sibling named
	 * `YYYYMMDD_HHMMSS_llama-server_workbench.log` so it (a) survives the
	 * restart and (b) matches the `*_workbench.log` retention glob and is
	 * cleaned up with the other logs. No-op when the log is missing or empty.
	 *
	 * Called at the start of launch() on every platform.
	 */
	static void backupServerLog();

	/**
	 * @brief Set a callback to receive stdout/stderr output from the server.
	 * @param callback Function that receives each line of output.
	 */
	void setOutputCallback(std::function<void(const std::string &)> callback);

	/**
	 * @brief Check if a model is currently loaded via API.
	 * @return true if server running and model loaded, false otherwise
	 */
	bool isModelLoaded() override;

	/**
	 * @brief Get the path of the currently loaded model.
	 * @return Model path, empty if no model loaded
	 */
	std::string getLoadedModelPath() override;

	/**
	 * @brief Get the ids (models.ini section names) of every loaded model.
	 *
	 * Queries /models once and returns every entry whose status is "loaded".
	 * The single-model @ref getLoadedModelPath is the first such id; this is the
	 * multi-model form used by the monitor to poll each loaded model in turn.
	 *
	 * @return Loaded model ids; empty when none are loaded or server
	 * unreachable.
	 */
	std::vector<std::string> getLoadedModelIds() override;

	/**
	 * @brief Unload the currently loaded model via API.
	 * @return true if unload succeeded, false on failure
	 */
	bool unloadModel() override;

	/**
	 * @brief Unload a specific model by name (models.ini section name) via API.
	 * @param modelName The models.ini section name of the model to unload.
	 * @return true if unload succeeded, false on failure.
	 */
	bool unloadModel(const std::string &modelName) override;

	/**
	 * @brief Unload every currently-loaded model via API.
	 * @return true if all unloads succeeded (also true when none were loaded).
	 */
	bool unloadAllModels() override;

	/**
	 * @brief Load a model via API (hot-swap without restart).
	 * @param modelPath Path to the .gguf model file
	 * @return true if load succeeded, false on failure
	 */
	bool loadModel(const std::string &modelPath) override;

	/**
	 * @brief Check if llama-server is responding via HTTP.
	 * @return true if server is healthy, false otherwise
	 */
	bool isServerHealthy() override;

	/**
	 * @brief Get the current slot status as JSON.
	 * @param modelName The model name to query slots for (required -
	 * llama-server requires it)
	 * @return JSON string with slot info, empty on failure
	 */
	std::string getSlotStatus(const std::string &modelName);

	/**
	 * @brief Probe a loaded model's worker process and classify its liveness.
	 *
	 * In router mode a worker can crash (e.g. CUDA OOM) while the router parent
	 * stays up; the router does not detect the dead child, so /models keeps
	 * reporting the model as "loaded" (llama.cpp #18912). The only reliable
	 * signal is a request proxied to the worker. This issues a proxied
	 * GET /slots?model=NAME and maps the HTTP outcome to @ref WorkerState so a
	 * busy worker (timeout, slow decode) is never mistaken for a crashed one
	 * (refused connection). An empty model name yields BUSY (neutral).
	 *
	 * @param modelName The model whose worker to probe (required).
	 * @return ALIVE on HTTP 200, DEAD on a refused/closed connection, BUSY on a
	 *         timeout or any ambiguous error.
	 */
	[[nodiscard]] WorkerState probeModelWorker(const std::string &modelName);

  private:
	class Impl; // Forward declaration for pImpl
	std::unique_ptr<Impl> m_impl;
};