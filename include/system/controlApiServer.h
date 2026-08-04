#pragma once

#include "IControlApiServer.h"
#include "apiSettings.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace httplib {
class Server;
}

/**
 * @file controlApiServer.h
 * @brief Inbound HTTP listener that exposes Workbench control endpoints.
 *
 * Owns an httplib::Server running on a dedicated thread. This is Workbench's
 * *own* API (distinct from the supervised llama-server): external clients use
 * it to control the running app.
 *
 * Route handlers are registered via @c registerControlApiRoutes (apiRoutes.h)
 * inside @c start(), after bind succeeds and before the accept thread launches.
 * Issue #106 adds @c POST /server/restart; further routes (#107 model
 * load/unload, #108 config) extend apiRoutes.h without touching this class.
 *
 * Thread model: start() binds the socket synchronously (so bind failure is
 * reported to the caller), then runs the blocking accept loop on @c m_thread.
 * stop() asks the server to stop and joins the thread. All public methods are
 * safe to call from the UI thread.
 */

/**
 * @class ControlApiServer
 * @brief Singleton lifecycle wrapper around an httplib::Server.
 */
class ControlApiServer : public IControlApiServer
{
  public:
	/**
	 * @brief Returns the process-wide singleton instance.
	 *
	 * Meyers' singleton — thread-safe lazy initialization.
	 */
	static ControlApiServer &instance();

	/**
	 * @brief Start the listener bound to @p settings.apiHost:apiPort.
	 *
	 * Idempotent: a no-op (returns true) when already running. Binds the socket
	 * before launching the accept thread, so a bind failure (port in use,
	 * invalid host) returns false and leaves the server stopped.
	 *
	 * @param settings Host/port for the listener.
	 * @return true if running after the call; false on bind failure.
	 */
	bool start(const Config::ApiSettings &settings) override;

	/**
	 * @brief Stop the listener and join the accept thread. Idempotent.
	 */
	void stop() override;

	/**
	 * @brief Whether the accept thread is currently running.
	 */
	[[nodiscard]] bool isRunning() const override;

  private:
	ControlApiServer();
	~ControlApiServer() override;

	// Non-copyable, non-movable (singleton owning a thread + socket).
	ControlApiServer(const ControlApiServer &) = delete;
	ControlApiServer &operator=(const ControlApiServer &) = delete;

	// Serializes start()/stop() so the lifecycle can't be raced from two
	// threads (e.g. UI toggle vs app shutdown).
	mutable std::mutex m_mutex;

	// httplib::Server is forward-declared; held by pointer to keep the heavy
	// header out of this interface. Recreated per start() because httplib's
	// Server is single-shot after stop().
	std::unique_ptr<httplib::Server> m_server;
	std::thread m_thread;
	std::atomic<bool> m_running{ false };
};
