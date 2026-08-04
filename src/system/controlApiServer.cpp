/**
 * @file controlApiServer.cpp
 * @brief Implementation of the inbound control-API listener.
 *
 * Issue #114 skeleton promoted by issue #106: routes are now registered via
 * apiRoutes before the accept thread starts. start() binds and runs an
 * httplib::Server accept loop on a background thread; stop() tears it down.
 */

#include "controlApiServer.h"

#include "apiRoutes.h"
#include "batchStore.h"
#include "configManager.h"
#include "httplib.h"
#include "llamaServerProcess.h"
#include "modelStateTracker.h"
#include "modelsIni.h"

#include <spdlog/spdlog.h>

ControlApiServer &ControlApiServer::instance()
{
	static ControlApiServer server;
	return server;
}

ControlApiServer::ControlApiServer() = default;

ControlApiServer::~ControlApiServer()
{
	stop();
}

bool ControlApiServer::start(const Config::ApiSettings &settings)
{
	std::scoped_lock lock(m_mutex);

	if (m_running.load(std::memory_order_acquire)) {
		spdlog::debug("ControlApiServer::start - already running, no-op");
		return true;
	}

	auto server = std::make_unique<httplib::Server>();

	// Disable httplib's default SO_REUSEPORT/SO_REUSEADDR. The control API is a
	// single exclusive listener; without reuse, a port already in use (another
	// instance, a stale process) makes bind fail loudly instead of silently
	// sharing the port. The no-op callback replaces default_socket_options.
	server->set_socket_options([](auto) {});

	// Bind synchronously so a failure (port in use, bad host) is reported to the
	// caller before we spawn the accept thread.
	if (!server->bind_to_port(settings.apiHost, settings.apiPort)) {
		spdlog::warn("ControlApiServer::start - failed to bind {}:{}",
					 settings.apiHost,
					 settings.apiPort);
		return false;
	}

	// Register route handlers (issue #106). Must happen after bind_to_port
	// succeeds and before listen_after_bind() is called on the accept thread.
	// Dependencies are resolved from the process-wide singletons; the deps
	// struct holds non-owning references that outlive the server.
	ControlApiDeps deps{
		LlamaServerProcess::instance(), ConfigManager::instance(),
		ModelsIni::instance(),			BatchStore::instance(),
		ModelStateTracker::instance(),	settings
	};
	registerControlApiRoutes(*server, deps);

	m_server = std::move(server);
	m_running.store(true, std::memory_order_release);

	// listen_after_bind() blocks until stop() is called on the server.
	m_thread = std::thread([this] {
		m_server->listen_after_bind();
		m_running.store(false, std::memory_order_release);
	});

	// Block until the accept loop is actually running. Without this, an
	// immediate stop() can race ahead of listen_after_bind(): httplib's stop()
	// would no-op (server not yet "running"), listen would then block forever,
	// and the join() in stop() would deadlock.
	m_server->wait_until_ready();

	spdlog::info("ControlApiServer started on {}:{}",
				 settings.apiHost,
				 settings.apiPort);
	return true;
}

void ControlApiServer::stop()
{
	std::scoped_lock lock(m_mutex);

	if (m_server)
		m_server->stop(); // unblocks listen_after_bind()

	if (m_thread.joinable())
		m_thread.join();

	m_server.reset();
	m_running.store(false, std::memory_order_release);
}

bool ControlApiServer::isRunning() const
{
	return m_running.load(std::memory_order_acquire);
}
