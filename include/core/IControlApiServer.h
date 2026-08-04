#pragma once

namespace Config {
struct ApiSettings;
}

/**
 * @file IControlApiServer.h
 * @brief Interface for Workbench's inbound control-API server.
 *
 * Callers (the Settings panel, app lifecycle) depend on this interface rather
 * than the ControlApiServer singleton directly, enabling unit testing with
 * GMock. The real ControlApiServer implements this interface directly.
 */

/**
 * @class IControlApiServer
 * @brief Start/stop control surface for the inbound HTTP listener.
 */
class IControlApiServer
{
  public:
	virtual ~IControlApiServer() = default;

	/**
	 * @brief Start the listener bound to @p settings.apiHost:apiPort.
	 *
	 * Idempotent: calling start() while already running is a no-op. The listener
	 * runs on its own thread; this call returns once the bind has been attempted.
	 *
	 * @param settings Host/port (and, in later tickets, auth) for the listener.
	 * @return true if the server is running after the call, false on bind failure.
	 */
	virtual bool start(const Config::ApiSettings &settings) = 0;

	/**
	 * @brief Stop the listener and join its thread. Idempotent.
	 */
	virtual void stop() = 0;

	/**
	 * @brief Whether the listener thread is currently running.
	 */
	[[nodiscard]] virtual bool isRunning() const = 0;
};
