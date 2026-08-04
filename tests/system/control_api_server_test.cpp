#include "controlApiServer.h"

#include "apiSettings.h"
#include "httplib.h"

#include <gtest/gtest.h>

#include <thread>

/**
 * @file control_api_server_test.cpp
 * @brief Lifecycle tests for the inbound control-API server (issue #114).
 *
 * The skeleton registers no routes, so these tests cover only start/stop/
 * isRunning and the bind-failure path. ControlApiServer is a singleton; each
 * test stops it in TearDown so state does not bleed between cases.
 */

namespace {

// High, fixed ports on localhost to avoid firewall prompts and clashes with
// real services. Localhost binding keeps the test off external interfaces.
constexpr int kPort = 18181;
constexpr const char *kHost = "127.0.0.1";

Config::ApiSettings makeSettings(int port = kPort)
{
	Config::ApiSettings s;
	s.apiHost = kHost;
	s.apiPort = port;
	s.apiEnabled = true;
	return s;
}

} // namespace

class ControlApiServerTest : public ::testing::Test
{
  protected:
	void TearDown() override
	{
		ControlApiServer::instance().stop();
	}
};

TEST_F(ControlApiServerTest, Start_SetsRunning)
{
	auto &server = ControlApiServer::instance();
	EXPECT_FALSE(server.isRunning());

	EXPECT_TRUE(server.start(makeSettings()));
	EXPECT_TRUE(server.isRunning());
}

TEST_F(ControlApiServerTest, Stop_ClearsRunning)
{
	auto &server = ControlApiServer::instance();
	ASSERT_TRUE(server.start(makeSettings()));

	server.stop();
	EXPECT_FALSE(server.isRunning());
}

TEST_F(ControlApiServerTest, Start_WhenAlreadyRunning_IsNoOp)
{
	auto &server = ControlApiServer::instance();
	ASSERT_TRUE(server.start(makeSettings()));

	// Second start returns true (already running) without rebinding.
	EXPECT_TRUE(server.start(makeSettings()));
	EXPECT_TRUE(server.isRunning());
}

TEST_F(ControlApiServerTest, Stop_WhenNotRunning_IsNoOp)
{
	auto &server = ControlApiServer::instance();
	ASSERT_FALSE(server.isRunning());

	// Must not crash or block.
	server.stop();
	EXPECT_FALSE(server.isRunning());
}

TEST_F(ControlApiServerTest, Start_BindFailure_ReturnsFalse)
{
	// Occupy the port with a separate httplib server, then assert the
	// ControlApiServer cannot bind it.
	httplib::Server blocker;
	ASSERT_TRUE(blocker.bind_to_port(kHost, kPort));
	std::thread blockerThread([&blocker] { blocker.listen_after_bind(); });

	// Wait until the blocker's accept loop is actually running before anyone
	// calls stop() on it. Without this, blocker.stop() below can race ahead of
	// listen_after_bind(): httplib's stop() no-ops while the server is not yet
	// "running", listen then blocks forever, and blockerThread.join() deadlocks.
	blocker.wait_until_ready();

	auto &server = ControlApiServer::instance();
	EXPECT_FALSE(server.start(makeSettings()));
	EXPECT_FALSE(server.isRunning());

	blocker.stop();
	blockerThread.join();
}

TEST_F(ControlApiServerTest, StartStopCycle_CanRestart)
{
	auto &server = ControlApiServer::instance();

	ASSERT_TRUE(server.start(makeSettings()));
	server.stop();
	ASSERT_FALSE(server.isRunning());

	// A fresh httplib::Server is created per start(), so a restart succeeds.
	EXPECT_TRUE(server.start(makeSettings()));
	EXPECT_TRUE(server.isRunning());
}
