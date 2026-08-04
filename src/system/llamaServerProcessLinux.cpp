/**
 * @file llamaServerProcessLinux.cpp
 * @brief Linux-specific implementation for launching llama-server.
 *
 * Uses fork() + execve() to spawn the llama-server process.
 * Captures stdout/stderr via pipes and forwards to a callback.
 */

#include "configManager.h"
#include "llamaServerProcess.h"
#include "modelInfoMonitor.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <json.hpp>
#include <mutex>
#include <signal.h>
#include <spdlog/spdlog.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

class LlamaServerProcess::Impl
{
  public:
	Impl() : pid_(-1), httpClient_()
	{
	}

	~Impl()
	{
		if (running_.load(std::memory_order_acquire)) {
			terminate();
		}
	}

	void setOutputCallback(std::function<void(const std::string &)> callback)
	{
		outputCallback_ = callback;
	}

	bool launch(const std::string &modelPath,
				const Config::ServerSettings &server)
	{
		// Build command arguments
		auto args = buildCommandArgs(modelPath, server);

		spdlog::debug("Building llama-server command");

		spdlog::info("Starting llama-server with model: '{}'", modelPath);

		// Prepare argv BEFORE fork(): this process is multithreaded, so the
		// child may only call async-signal-safe functions between fork() and
		// execve(). Building the vector here keeps malloc out of the child,
		// where another thread's allocator lock could be held at fork time.
		std::vector<char *> argv;
		argv.reserve(args.size() + 1);
		for (const auto &arg : args) {
			argv.push_back(const_cast<char *>(arg.c_str()));
		}
		argv.push_back(nullptr);
		const char *exePath = server.executablePath.empty()
								  ? "llama-server"
								  : server.executablePath.c_str();

		// Create pipes for stdout and stderr with O_CLOEXEC so the exec'd
		// child does not inherit the read ends (dup2 below clears the flag on
		// the stdio copies, which is exactly what we want).
		int stdoutPipe[2];
		int stderrPipe[2];

		if (pipe2(stdoutPipe, O_CLOEXEC) < 0) {
			spdlog::error("Failed to create stdout pipe: {}", strerror(errno));
			return false;
		}

		if (pipe2(stderrPipe, O_CLOEXEC) < 0) {
			close(stdoutPipe[0]);
			close(stdoutPipe[1]);
			spdlog::error("Failed to create stderr pipe: {}", strerror(errno));
			return false;
		}

		std::lock_guard<std::mutex> lock(stateMutex_);

		// Fork the process
		pid_t pid = fork();
		if (pid < 0) {
			close(stdoutPipe[0]);
			close(stdoutPipe[1]);
			close(stderrPipe[0]);
			close(stderrPipe[1]);
			spdlog::error("Failed to start llama-server: fork() failed: {}",
						  strerror(errno));
			return false;
		}

		if (pid == 0) {
			// Child process — async-signal-safe calls only from here to exec.
			dup2(stdoutPipe[1], STDOUT_FILENO);
			dup2(stderrPipe[1], STDERR_FILENO);

			// Ask the kernel to send SIGKILL to this child when the parent dies
			prctl(PR_SET_PDEATHSIG, SIGKILL);

			// Detach from controlling terminal
			setsid();

			execve(exePath, argv.data(), environ);
			// If we get here, execve failed
			_exit(127);
		}

		// Parent process - close write ends and start reading
		close(stdoutPipe[1]);
		close(stderrPipe[1]);

		spdlog::info("llama-server started (PID: {})", pid);
		pid_ = pid;
		running_.store(true, std::memory_order_release);

		// Start background thread to read from pipes
		// Copy file descriptors to avoid capture issues with arrays
		int stdoutFd = stdoutPipe[0];
		int stderrFd = stderrPipe[0];
		stopPipeReader_.store(false, std::memory_order_release);
		// The reader thread takes ownership of the read-end fds (readPipes()
		// close()s them when it finishes). If std::thread construction
		// throws, that ownership transfer never happens, so close the fds
		// here to avoid leaking them, and unwind the launched child.
		try {
			pipeReadThread_ = std::thread(
				[this, stdoutFd, stderrFd]() { readPipes(stdoutFd, stderrFd); });
		} catch (const std::system_error &e) {
			close(stdoutFd);
			close(stderrFd);
			spdlog::error("Failed to start pipe reader thread: {}", e.what());
			terminateLocked();
			return false;
		}

		return true;
	}

	bool terminate()
	{
		std::lock_guard<std::mutex> lock(stateMutex_);
		return terminateLocked();
	}

	bool isRunning()
	{
		if (!running_.load(std::memory_order_acquire)) {
			return false;
		}
		std::lock_guard<std::mutex> lock(stateMutex_);
		if (pid_ < 0) {
			return false;
		}
		int status;
		pid_t result = waitpid(pid_, &status, WNOHANG);
		if (result == 0) {
			// Child still running
			return true;
		}
		// Child exited (result == pid_) or is gone (-1/ECHILD). The waitpid
		// above reaped it, so the kernel may recycle the PID at any moment —
		// record the reap NOW or a later terminate() would signal a stranger.
		pid_ = -1;
		running_.store(false, std::memory_order_release);
		return false;
	}

	intptr_t getHandle() const
	{
		std::lock_guard<std::mutex> lock(stateMutex_);
		return static_cast<intptr_t>(pid_);
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
	/**
	 * @brief Terminate the child. @pre stateMutex_ is held by the caller.
	 */
	bool terminateLocked()
	{
		if (!running_.load(std::memory_order_acquire) || pid_ < 0) {
			return false;
		}

		spdlog::info("Terminating llama-server...");

		// Stop the pipe reader first
		stopPipeReader_.store(true, std::memory_order_release);
		if (pipeReadThread_.joinable()) {
			pipeReadThread_.join();
		}

		// Try graceful termination first with SIGTERM
		if (kill(pid_, SIGTERM) == 0) {
			// Wait up to 5 seconds for graceful shutdown
			for (int i = 0; i < 50; ++i) {
				int status;
				pid_t result = waitpid(pid_, &status, WNOHANG);
				if (result == pid_) {
					running_.store(false, std::memory_order_release);
					pid_ = -1;
					spdlog::info("llama-server terminated");
					return true;
				}
				usleep(100000); // 100ms
			}
		}

		// Force kill if still running
		if (kill(pid_, SIGKILL) == 0) {
			waitpid(pid_, nullptr, 0);
		}

		running_.store(false, std::memory_order_release);
		pid_ = -1;
		spdlog::info("llama-server terminated");
		return true;
	}

	/**
	 * @brief Drain BOTH child pipes from one loop.
	 *
	 * stdout and stderr must be serviced together: draining them
	 * sequentially lets the unserviced pipe fill (64 KiB) and then blocks
	 * the child on write(), wedging the server. select() watches whichever
	 * ends are still open; the loop exits when both hit EOF or the stop
	 * flag is set. Owns both fds and closes them on exit.
	 */
	void readPipes(int stdoutFd, int stderrFd)
	{
		constexpr size_t bufferSize = 4096;
		char buffer[bufferSize];
		int fds[2] = { stdoutFd, stderrFd };
		std::string lineBuffers[2];
		bool open[2] = { true, true };

		while (!stopPipeReader_.load(std::memory_order_acquire) &&
			   (open[0] || open[1])) {
			fd_set readfds;
			FD_ZERO(&readfds);
			int maxFd = -1;
			for (int i = 0; i < 2; ++i) {
				if (open[i]) {
					FD_SET(fds[i], &readfds);
					maxFd = std::max(maxFd, fds[i]);
				}
			}

			struct timeval tv = { 0, 100000 }; // 100ms timeout
			int sel = select(maxFd + 1, &readfds, nullptr, nullptr, &tv);
			if (sel < 0) {
				if (errno == EINTR)
					continue;
				break;
			}
			if (sel == 0)
				continue;

			for (int i = 0; i < 2; ++i) {
				if (!open[i] || !FD_ISSET(fds[i], &readfds))
					continue;

				ssize_t bytesRead = read(fds[i], buffer, bufferSize);
				if (bytesRead < 0) {
					if (errno == EINTR || errno == EAGAIN)
						continue;
					open[i] = false;
					continue;
				}
				if (bytesRead == 0) { // EOF — child closed this end
					open[i] = false;
					continue;
				}

				// Process character by character to handle line breaks
				for (ssize_t j = 0; j < bytesRead; ++j) {
					if (buffer[j] == '\n' || buffer[j] == '\r') {
						if (!lineBuffers[i].empty()) {
							if (outputCallback_) {
								outputCallback_(lineBuffers[i]);
							}
							lineBuffers[i].clear();
						}
					} else {
						lineBuffers[i] += buffer[j];
					}
				}
			}
		}

		// Send any remaining data and release the fds
		for (int i = 0; i < 2; ++i) {
			if (!lineBuffers[i].empty() && outputCallback_) {
				outputCallback_(lineBuffers[i]);
			}
			close(fds[i]);
		}
	}

	// pid_ and the pid_<->running_ transitions are guarded by stateMutex_;
	// running_ is additionally atomic so hot paths (isServerHealthy on the
	// poll thread) can read it without taking the lock.
	pid_t pid_;
	std::atomic<bool> running_{ false };
	mutable std::mutex stateMutex_;
	std::function<void(const std::string &)> outputCallback_;
	std::thread pipeReadThread_;
	std::atomic<bool> stopPipeReader_{ false };
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
	m_impl->setOutputCallback(callback);
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