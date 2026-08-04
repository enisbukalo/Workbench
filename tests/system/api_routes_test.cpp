/**
 * @file api_routes_test.cpp
 * @brief Unit tests for the control-API route handlers (issue #106).
 *
 * Routes are exercised by registering them on a local httplib::Server bound to
 * a fixed localhost port, then hitting it with httplib::Client. Dependencies
 * are injected via GMock mocks so no real llama-server process or config file
 * is needed.
 *
 * Port 18190 is chosen to be distinct from control_api_server_test.cpp (18181)
 * and far from any real service.
 */

#include "apiRoutes.h"

#include "MockConfigManager.h"
#include "MockLlamaServerProcess.h"
#include "MockModelStateTracker.h"
#include "MockBatchStore.h"
#include "MockModelsIni.h"

#include "httplib.h"
#include "json.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <thread>
#include <vector>

using ::testing::_;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::Return;

namespace {

constexpr int         kPort = 18190;
constexpr const char *kHost = "127.0.0.1";

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

/**
 * @brief Fixture that spins up a real httplib::Server on 127.0.0.1:18190 for
 * each test, registers routes via @c registerControlApiRoutes using mock
 * dependencies, and tears it down in TearDown.
 *
 * Mocks use NiceMock<T> to suppress uninteresting-call warnings for methods
 * tests do not explicitly configure.
 */
class ApiRoutesTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        m_svr = std::make_unique<httplib::Server>();
        // Keep httplib's default SO_REUSEADDR so each test can immediately
        // rebind kPort after the previous test's socket enters TIME_WAIT.
        // (Production ControlApiServer disables reuse to fail loudly on a port
        // clash; tests intentionally do not.)

        ASSERT_TRUE(m_svr->bind_to_port(kHost, kPort))
            << "Could not bind 127.0.0.1:" << kPort
            << " — is another test already using that port?";

        registerControlApiRoutes(*m_svr, makeDeps());

        m_thread = std::thread([this] { m_svr->listen_after_bind(); });
        m_svr->wait_until_ready();
    }

    void TearDown() override
    {
        if (m_svr)
            m_svr->stop();
        if (m_thread.joinable())
            m_thread.join();
    }

    // -----------------------------------------------------------------------
    // Accessors for setting up mock expectations in individual tests

    NiceMock<MockLlamaServerProcess> &mockServer()    { return m_mockServer; }
    NiceMock<MockConfigManager>      &mockConfig()    { return m_mockConfig; }
    NiceMock<MockModelsIni>          &mockModels()    { return m_mockModelsIni; }
    NiceMock<MockBatchStore>         &mockBatches()   { return m_mockBatches; }
    NiceMock<MockModelStateTracker>  &mockTracker()   { return m_mockTracker; }

    // -----------------------------------------------------------------------
    // Helper: build a ControlApiDeps pointing at the fixture's mocks

    ControlApiDeps makeDeps(bool requireKey = false, const std::string &key = "")
    {
        Config::ApiSettings api;
        api.apiRequireKey = requireKey;
        api.apiKey        = key;
        return ControlApiDeps{
            m_mockServer,
            m_mockConfig,
            m_mockModelsIni,
            m_mockBatches,
            m_mockTracker,
            api
        };
    }

    // -----------------------------------------------------------------------
    // Helper: POST /server/restart and return the (status, parsed-body) pair

    struct PostResult
    {
        int              status{};
        nlohmann::json   body;
    };

    PostResult postRestart(const std::string &authHeader = "")
    {
        httplib::Client cli(kHost, kPort);
        cli.set_connection_timeout(2);
        cli.set_read_timeout(2);

        httplib::Headers headers;
        if (!authHeader.empty())
            headers.emplace("Authorization", authHeader);

        auto res = cli.Post("/server/restart", headers, "", "application/json");
        EXPECT_TRUE(static_cast<bool>(res)) << "httplib::Client::Post failed";

        PostResult out;
        out.status = res->status;
        out.body   = nlohmann::json::parse(res->body, nullptr, /*exceptions=*/false);
        return out;
    }

  private:
    NiceMock<MockLlamaServerProcess> m_mockServer;
    NiceMock<MockConfigManager>      m_mockConfig;
    NiceMock<MockModelsIni>          m_mockModelsIni;
    NiceMock<MockBatchStore>         m_mockBatches;
    NiceMock<MockModelStateTracker>  m_mockTracker;

    std::unique_ptr<httplib::Server> m_svr;
    std::thread                      m_thread;
};

} // namespace

// ---------------------------------------------------------------------------
// POST /server/restart — auth-off path
// ---------------------------------------------------------------------------

/**
 * @brief With auth disabled the handler must call requestUnloadAll(),
 * terminate(), then launch() in that exact order and return 200 + "restarted".
 */
TEST_F(ApiRoutesTest, Restart_AuthOff_TerminatesAndLaunches)
{
    Config::ServerSettings serverCfg;
    ON_CALL(mockConfig(), getServerSettings()).WillByDefault(Return(serverCfg));
    ON_CALL(mockServer(), isRunning()).WillByDefault(Return(true));

    {
        InSequence seq;
        EXPECT_CALL(mockTracker(), requestUnloadAll()).Times(1);
        EXPECT_CALL(mockServer(), terminate()).Times(1).WillOnce(Return(true));
        EXPECT_CALL(mockServer(), launch("", _)).Times(1).WillOnce(Return(true));
    }

    auto [status, body] = postRestart();

    EXPECT_EQ(status, 200);
    EXPECT_EQ(body["status"].get<std::string>(), "restarted");
    EXPECT_TRUE(body["running"].get<bool>());
}

/**
 * @brief When launch() returns false the handler must return 500 with status
 * "failed" and running reflecting the mock's isRunning() state.
 */
TEST_F(ApiRoutesTest, Restart_LaunchFails_Returns500)
{
    Config::ServerSettings serverCfg;
    ON_CALL(mockConfig(), getServerSettings()).WillByDefault(Return(serverCfg));
    ON_CALL(mockServer(), isRunning()).WillByDefault(Return(false));
    ON_CALL(mockServer(), terminate()).WillByDefault(Return(true));
    ON_CALL(mockServer(), launch(_, _)).WillByDefault(Return(false));

    EXPECT_CALL(mockTracker(), requestUnloadAll()).Times(1);

    auto [status, body] = postRestart();

    EXPECT_EQ(status, 500);
    EXPECT_EQ(body["status"].get<std::string>(), "failed");
    EXPECT_FALSE(body["running"].get<bool>());
}

// ---------------------------------------------------------------------------
// POST /server/restart — auth-on paths
// ---------------------------------------------------------------------------

/**
 * @brief With auth enabled and no Authorization header the request must be
 * rejected 401 before any server methods are touched.
 */
TEST_F(ApiRoutesTest, Restart_AuthOn_MissingKey_Returns401)
{
    // Re-register routes with auth enabled. SetUp already registered open
    // routes; we need a new server for auth-on. Use a sub-scope server on a
    // separate port so we don't conflict with the fixture's port.
    httplib::Server authSvr;
    constexpr int kAuthPort = 18191;
    ASSERT_TRUE(authSvr.bind_to_port(kHost, kAuthPort));

    ControlApiDeps deps = makeDeps(/*requireKey=*/true, /*key=*/"secret");
    registerControlApiRoutes(authSvr, deps);

    std::thread t([&authSvr] { authSvr.listen_after_bind(); });
    authSvr.wait_until_ready();

    // Server-side mocks must NOT be called.
    EXPECT_CALL(mockTracker(), requestUnloadAll()).Times(0);
    EXPECT_CALL(mockServer(), terminate()).Times(0);
    EXPECT_CALL(mockServer(), launch(_, _)).Times(0);

    httplib::Client cli(kHost, kAuthPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res = cli.Post("/server/restart", "", "application/json");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 401);

    authSvr.stop();
    t.join();
}

/**
 * @brief With auth enabled and an incorrect key the request must be 401 and
 * no server methods may be invoked.
 */
TEST_F(ApiRoutesTest, Restart_AuthOn_WrongKey_Returns401)
{
    httplib::Server authSvr;
    constexpr int kAuthPort = 18192;
    ASSERT_TRUE(authSvr.bind_to_port(kHost, kAuthPort));

    ControlApiDeps deps = makeDeps(/*requireKey=*/true, /*key=*/"secret");
    registerControlApiRoutes(authSvr, deps);

    std::thread t([&authSvr] { authSvr.listen_after_bind(); });
    authSvr.wait_until_ready();

    EXPECT_CALL(mockTracker(), requestUnloadAll()).Times(0);
    EXPECT_CALL(mockServer(), terminate()).Times(0);
    EXPECT_CALL(mockServer(), launch(_, _)).Times(0);

    httplib::Client cli(kHost, kAuthPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    httplib::Headers headers;
    headers.emplace("Authorization", "Bearer wrong");
    auto res = cli.Post("/server/restart", headers, "", "application/json");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 401);

    authSvr.stop();
    t.join();
}

/**
 * @brief With auth enabled and the correct key the handler succeeds with 200.
 */
TEST_F(ApiRoutesTest, Restart_AuthOn_CorrectKey_Succeeds)
{
    httplib::Server authSvr;
    constexpr int kAuthPort = 18193;
    ASSERT_TRUE(authSvr.bind_to_port(kHost, kAuthPort));

    ControlApiDeps deps = makeDeps(/*requireKey=*/true, /*key=*/"secret");
    registerControlApiRoutes(authSvr, deps);

    std::thread t([&authSvr] { authSvr.listen_after_bind(); });
    authSvr.wait_until_ready();

    Config::ServerSettings serverCfg;
    ON_CALL(mockConfig(), getServerSettings()).WillByDefault(Return(serverCfg));
    ON_CALL(mockServer(), isRunning()).WillByDefault(Return(true));
    ON_CALL(mockServer(), terminate()).WillByDefault(Return(true));
    ON_CALL(mockServer(), launch(_, _)).WillByDefault(Return(true));

    EXPECT_CALL(mockTracker(), requestUnloadAll()).Times(1);

    httplib::Client cli(kHost, kAuthPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    httplib::Headers headers;
    headers.emplace("Authorization", "Bearer secret");
    auto res = cli.Post("/server/restart", headers, "", "application/json");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 200);

    auto body = nlohmann::json::parse(res->body, nullptr, false);
    EXPECT_EQ(body["status"].get<std::string>(), "restarted");

    authSvr.stop();
    t.join();
}

// ---------------------------------------------------------------------------
// isAuthorized — direct unit tests
// ---------------------------------------------------------------------------

/**
 * @brief isAuthorized returns true when apiRequireKey is false, regardless of
 * headers.
 */
TEST(IsAuthorized, AuthDisabled_AlwaysTrue)
{
    Config::ApiSettings settings;
    settings.apiRequireKey = false;
    settings.apiKey        = "secret";

    httplib::Request req;
    EXPECT_TRUE(isAuthorized(req, settings));
}

/**
 * @brief isAuthorized returns false when auth is required and no header is
 * present.
 */
TEST(IsAuthorized, AuthEnabled_NoHeader_ReturnsFalse)
{
    Config::ApiSettings settings;
    settings.apiRequireKey = true;
    settings.apiKey        = "secret";

    httplib::Request req;
    EXPECT_FALSE(isAuthorized(req, settings));
}

/**
 * @brief isAuthorized returns false when the Authorization header value has
 * no "Bearer " prefix.
 */
TEST(IsAuthorized, AuthEnabled_MissingBearerPrefix_ReturnsFalse)
{
    Config::ApiSettings settings;
    settings.apiRequireKey = true;
    settings.apiKey        = "secret";

    httplib::Request req;
    req.headers.emplace("Authorization", "secret");
    EXPECT_FALSE(isAuthorized(req, settings));
}

/**
 * @brief isAuthorized returns false when the key after "Bearer " does not
 * match.
 */
TEST(IsAuthorized, AuthEnabled_WrongKey_ReturnsFalse)
{
    Config::ApiSettings settings;
    settings.apiRequireKey = true;
    settings.apiKey        = "secret";

    httplib::Request req;
    req.headers.emplace("Authorization", "Bearer wrong");
    EXPECT_FALSE(isAuthorized(req, settings));
}

/**
 * @brief isAuthorized returns true when auth is required and the exact key is
 * presented.
 */
TEST(IsAuthorized, AuthEnabled_CorrectKey_ReturnsTrue)
{
    Config::ApiSettings settings;
    settings.apiRequireKey = true;
    settings.apiKey        = "secret";

    httplib::Request req;
    req.headers.emplace("Authorization", "Bearer secret");
    EXPECT_TRUE(isAuthorized(req, settings));
}

/**
 * @brief isAuthorized is case-sensitive: "SECRET" != "secret".
 */
TEST(IsAuthorized, AuthEnabled_KeyIsCaseSensitive)
{
    Config::ApiSettings settings;
    settings.apiRequireKey = true;
    settings.apiKey        = "secret";

    httplib::Request req;
    req.headers.emplace("Authorization", "Bearer SECRET");
    EXPECT_FALSE(isAuthorized(req, settings));
}

// ---------------------------------------------------------------------------
// GET /models
// ---------------------------------------------------------------------------

namespace {

/// Build a populated ModelPreset for use in GET /models tests.
Config::ModelPreset makeTestPreset(const std::string &name,
                                   const std::string &modelPath,
                                   int ctxSize)
{
    Config::ModelPreset p;
    p.name             = name;
    p.model            = modelPath;
    p.load.modelPath   = modelPath;
    p.load.ctxSize     = ctxSize;
    p.load.ngpuLayers  = 32;
    p.load.batchSize   = 512;
    p.load.parallel    = 2;
    p.load.flashAttn   = "on";
    p.load.cacheTypeK  = "f16";
    p.load.cacheTypeV  = "f16";
    p.inference.temperature = 0.7;
    p.inference.topK        = 20;
    p.inference.topP        = 0.9;
    p.inference.nPredict    = 256;
    return p;
}

} // namespace

/**
 * @brief GET /models returns a 200 response with a models array containing
 * one object per preset returned by getModelNames/getPreset.
 */
TEST_F(ApiRoutesTest, GetModels_ReturnsList)
{
    ON_CALL(mockModels(), getModelNames())
        .WillByDefault(Return(std::vector<std::string>{"alpha", "beta"}));
    ON_CALL(mockModels(), getPreset("alpha"))
        .WillByDefault(Return(std::optional<Config::ModelPreset>{
            makeTestPreset("alpha", "/models/alpha.gguf", 4096)}));
    ON_CALL(mockModels(), getPreset("beta"))
        .WillByDefault(Return(std::optional<Config::ModelPreset>{
            makeTestPreset("beta", "/models/beta.gguf", 8192)}));

    httplib::Client cli(kHost, kPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res = cli.Get("/models");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 200);

    const auto body = nlohmann::json::parse(res->body, nullptr, false);
    ASSERT_TRUE(body.contains("models"));
    ASSERT_EQ(body["models"].size(), 2u);
    EXPECT_EQ(body["models"][0]["name"].get<std::string>(), "alpha");
    EXPECT_EQ(body["models"][0]["load"]["ctxSize"].get<int>(), 4096);
    EXPECT_EQ(body["models"][1]["name"].get<std::string>(), "beta");
}

/**
 * @brief GET /models with auth enabled and no key returns 401.
 */
TEST_F(ApiRoutesTest, GetModels_AuthOn_MissingKey_Returns401)
{
    httplib::Server authSvr;
    constexpr int kAuthPort = 18194;
    ASSERT_TRUE(authSvr.bind_to_port(kHost, kAuthPort));

    ControlApiDeps deps = makeDeps(/*requireKey=*/true, /*key=*/"secret");
    registerControlApiRoutes(authSvr, deps);

    std::thread t([&authSvr] { authSvr.listen_after_bind(); });
    authSvr.wait_until_ready();

    EXPECT_CALL(mockModels(), getModelNames()).Times(0);

    httplib::Client cli(kHost, kAuthPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res = cli.Get("/models");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 401);

    authSvr.stop();
    t.join();
}

// ---------------------------------------------------------------------------
// POST /models/load
// ---------------------------------------------------------------------------

/**
 * @brief POST /models/load with a known preset records load intent on the
 * tracker (requestLoad) then triggers loadModel() in that order; 200 "loading".
 */
TEST_F(ApiRoutesTest, LoadModel_KnownPreset_Returns200)
{
    ON_CALL(mockModels(), getModelNames())
        .WillByDefault(Return(std::vector<std::string>{"alpha"}));
    ON_CALL(mockServer(), isModelLoaded()).WillByDefault(Return(false));

    {
        InSequence seq;
        EXPECT_CALL(mockTracker(), requestLoad(::testing::Eq(std::string("alpha"))))
            .Times(1);
        EXPECT_CALL(mockServer(), loadModel("alpha")).Times(1).WillOnce(Return(true));
    }

    httplib::Client cli(kHost, kPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res = cli.Post("/models/load", R"({"model":"alpha"})", "application/json");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 200);

    const auto body = nlohmann::json::parse(res->body, nullptr, false);
    EXPECT_EQ(body["status"].get<std::string>(), "loading");
    EXPECT_EQ(body["model"].get<std::string>(), "alpha");
}

/**
 * @brief POST /models/load with an unknown preset name returns 400 and must
 * not invoke loadModel().
 */
TEST_F(ApiRoutesTest, LoadModel_UnknownPreset_Returns400)
{
    ON_CALL(mockModels(), getModelNames())
        .WillByDefault(Return(std::vector<std::string>{"alpha"}));

    EXPECT_CALL(mockServer(), loadModel(_)).Times(0);

    httplib::Client cli(kHost, kPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res = cli.Post("/models/load", R"({"model":"unknown"})", "application/json");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 400);

    const auto body = nlohmann::json::parse(res->body, nullptr, false);
    EXPECT_TRUE(body["error"].get<std::string>().find("unknown model preset") !=
                std::string::npos);
}

/**
 * @brief POST /models/load when loadModel() returns false yields 502.
 */
TEST_F(ApiRoutesTest, LoadModel_LoadModelFails_Returns502)
{
    ON_CALL(mockModels(), getModelNames())
        .WillByDefault(Return(std::vector<std::string>{"alpha"}));
    ON_CALL(mockServer(), isModelLoaded()).WillByDefault(Return(false));
    ON_CALL(mockServer(), loadModel(_)).WillByDefault(Return(false));

    httplib::Client cli(kHost, kPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res = cli.Post("/models/load", R"({"model":"alpha"})", "application/json");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 502);

    const auto body = nlohmann::json::parse(res->body, nullptr, false);
    EXPECT_EQ(body["status"].get<std::string>(), "failed");
}

/**
 * @brief POST /models/load with a malformed JSON body returns 400.
 */
TEST_F(ApiRoutesTest, LoadModel_MalformedBody_Returns400)
{
    EXPECT_CALL(mockServer(), loadModel(_)).Times(0);

    httplib::Client cli(kHost, kPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);

    // Truncated JSON
    auto res = cli.Post("/models/load", "{", "application/json");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 400);
}

/**
 * @brief POST /models/load with a body that has no "model" field returns 400.
 */
TEST_F(ApiRoutesTest, LoadModel_MissingModelField_Returns400)
{
    EXPECT_CALL(mockServer(), loadModel(_)).Times(0);

    httplib::Client cli(kHost, kPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res = cli.Post("/models/load", R"({"foo":1})", "application/json");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 400);
}

/**
 * @brief POST /models/load with auth enabled and wrong key returns 401 and
 * must not invoke loadModel().
 */
TEST_F(ApiRoutesTest, LoadModel_AuthOn_WrongKey_Returns401)
{
    httplib::Server authSvr;
    constexpr int kAuthPort = 18195;
    ASSERT_TRUE(authSvr.bind_to_port(kHost, kAuthPort));

    ControlApiDeps deps = makeDeps(/*requireKey=*/true, /*key=*/"secret");
    registerControlApiRoutes(authSvr, deps);

    std::thread t([&authSvr] { authSvr.listen_after_bind(); });
    authSvr.wait_until_ready();

    EXPECT_CALL(mockServer(), loadModel(_)).Times(0);

    httplib::Client cli(kHost, kAuthPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    httplib::Headers headers;
    headers.emplace("Authorization", "Bearer wrong");
    auto res =
        cli.Post("/models/load", headers, R"({"model":"alpha"})", "application/json");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 401);

    authSvr.stop();
    t.join();
}

// ---------------------------------------------------------------------------
// POST /models/unload — all-models path (no body / no "model" field)
// ---------------------------------------------------------------------------

/**
 * @brief POST /models/unload with empty body calls requestUnloadAll() then
 * unloadAllModels() in that exact order and returns 200 with scope "all".
 */
TEST_F(ApiRoutesTest, UnloadAll_NoBody_Returns200)
{
    {
        InSequence seq;
        EXPECT_CALL(mockTracker(), requestUnloadAll()).Times(1);
        EXPECT_CALL(mockServer(), unloadAllModels())
            .Times(1)
            .WillOnce(Return(true));
    }

    httplib::Client cli(kHost, kPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res = cli.Post("/models/unload", "", "application/json");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 200);

    const auto body = nlohmann::json::parse(res->body, nullptr, false);
    EXPECT_EQ(body["status"].get<std::string>(), "unloaded");
    EXPECT_EQ(body["scope"].get<std::string>(), "all");
    EXPECT_FALSE(body["loaded"].get<bool>());
}

/**
 * @brief POST /models/unload with a JSON body that has no "model" key also
 * routes to the all-models path.
 */
TEST_F(ApiRoutesTest, UnloadAll_BodyWithoutModelKey_Returns200)
{
    {
        InSequence seq;
        EXPECT_CALL(mockTracker(), requestUnloadAll()).Times(1);
        EXPECT_CALL(mockServer(), unloadAllModels())
            .Times(1)
            .WillOnce(Return(true));
    }

    httplib::Client cli(kHost, kPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res = cli.Post("/models/unload", R"({"foo":1})", "application/json");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 200);

    const auto body = nlohmann::json::parse(res->body, nullptr, false);
    EXPECT_EQ(body["scope"].get<std::string>(), "all");
}

/**
 * @brief POST /models/unload when unloadAllModels() returns false yields 500
 * with status "failed".
 */
TEST_F(ApiRoutesTest, UnloadAll_UnloadAllFails_Returns500)
{
    ON_CALL(mockServer(), unloadAllModels()).WillByDefault(Return(false));

    httplib::Client cli(kHost, kPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res = cli.Post("/models/unload", "", "application/json");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 500);

    const auto body = nlohmann::json::parse(res->body, nullptr, false);
    EXPECT_EQ(body["status"].get<std::string>(), "failed");
    EXPECT_EQ(body["scope"].get<std::string>(), "all");
}

// ---------------------------------------------------------------------------
// POST /models/unload — single-model path (body carries "model" field)
// ---------------------------------------------------------------------------

/**
 * @brief POST /models/unload with a known model name records requestUnload(name)
 * then unloadModel(name) in that order; returns 200, scope "model".
 */
TEST_F(ApiRoutesTest, UnloadSingle_KnownModel_Returns200)
{
    ON_CALL(mockModels(), getModelNames())
        .WillByDefault(Return(std::vector<std::string>{"alpha"}));

    {
        InSequence seq;
        EXPECT_CALL(mockTracker(),
                    requestUnload(::testing::Eq(std::string("alpha"))))
            .Times(1);
        EXPECT_CALL(mockServer(), unloadModel(::testing::Eq(std::string("alpha"))))
            .Times(1)
            .WillOnce(Return(true));
    }

    httplib::Client cli(kHost, kPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res =
        cli.Post("/models/unload", R"({"model":"alpha"})", "application/json");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 200);

    const auto body = nlohmann::json::parse(res->body, nullptr, false);
    EXPECT_EQ(body["status"].get<std::string>(), "unloaded");
    EXPECT_EQ(body["scope"].get<std::string>(), "model");
    EXPECT_EQ(body["model"].get<std::string>(), "alpha");
    EXPECT_FALSE(body["loaded"].get<bool>());
}

/**
 * @brief POST /models/unload with an unknown model name returns 400 and must
 * not invoke unloadModel() or unloadAllModels().
 */
TEST_F(ApiRoutesTest, UnloadSingle_UnknownModel_Returns400)
{
    ON_CALL(mockModels(), getModelNames())
        .WillByDefault(Return(std::vector<std::string>{"alpha"}));

    EXPECT_CALL(mockServer(), unloadModel(::testing::An<const std::string &>()))
        .Times(0);
    EXPECT_CALL(mockServer(), unloadAllModels()).Times(0);

    httplib::Client cli(kHost, kPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res =
        cli.Post("/models/unload", R"({"model":"nope"})", "application/json");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 400);

    const auto body = nlohmann::json::parse(res->body, nullptr, false);
    EXPECT_TRUE(body["error"].get<std::string>().find("unknown model preset") !=
                std::string::npos);
}

/**
 * @brief POST /models/unload with an empty "model" string returns 400 and
 * must not invoke any unload method.
 */
TEST_F(ApiRoutesTest, UnloadSingle_EmptyModelField_Returns400)
{
    EXPECT_CALL(mockServer(), unloadModel(::testing::An<const std::string &>()))
        .Times(0);
    EXPECT_CALL(mockServer(), unloadAllModels()).Times(0);

    httplib::Client cli(kHost, kPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res =
        cli.Post("/models/unload", R"({"model":""})", "application/json");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 400);
}

/**
 * @brief POST /models/unload when unloadModel(name) returns false yields 500.
 */
TEST_F(ApiRoutesTest, UnloadSingle_UnloadFails_Returns500)
{
    ON_CALL(mockModels(), getModelNames())
        .WillByDefault(Return(std::vector<std::string>{"alpha"}));
    ON_CALL(mockServer(), unloadModel(::testing::An<const std::string &>()))
        .WillByDefault(Return(false));

    httplib::Client cli(kHost, kPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res =
        cli.Post("/models/unload", R"({"model":"alpha"})", "application/json");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 500);

    const auto body = nlohmann::json::parse(res->body, nullptr, false);
    EXPECT_EQ(body["status"].get<std::string>(), "failed");
    EXPECT_EQ(body["scope"].get<std::string>(), "model");
}

// ---------------------------------------------------------------------------
// POST /models/unload — auth guard
// ---------------------------------------------------------------------------

/**
 * @brief POST /models/unload with auth enabled and missing key returns 401
 * and must not record unload intent on the tracker or call any unload method.
 */
TEST_F(ApiRoutesTest, UnloadModel_AuthOn_MissingKey_Returns401)
{
    httplib::Server authSvr;
    constexpr int kAuthPort = 18196;
    ASSERT_TRUE(authSvr.bind_to_port(kHost, kAuthPort));

    ControlApiDeps deps = makeDeps(/*requireKey=*/true, /*key=*/"secret");
    registerControlApiRoutes(authSvr, deps);

    std::thread t([&authSvr] { authSvr.listen_after_bind(); });
    authSvr.wait_until_ready();

    EXPECT_CALL(mockTracker(), requestUnloadAll()).Times(0);
    EXPECT_CALL(mockTracker(), requestUnload(_)).Times(0);
    EXPECT_CALL(mockServer(), unloadModel()).Times(0);
    EXPECT_CALL(mockServer(), unloadModel(::testing::An<const std::string &>()))
        .Times(0);
    EXPECT_CALL(mockServer(), unloadAllModels()).Times(0);

    httplib::Client cli(kHost, kAuthPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res = cli.Post("/models/unload", "", "application/json");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 401);

    authSvr.stop();
    t.join();
}

// ---------------------------------------------------------------------------
// GET /config/app — read-only app settings with secret redaction (issue #108)
// ---------------------------------------------------------------------------

/**
 * @brief GET /config/app returns 200 and redacts non-empty secrets
 * (server.apiKey, api.apiKey) to the sentinel while leaving empty secrets
 * (server.sslKeyFile) empty.
 */
TEST_F(ApiRoutesTest, GetConfigApp_RedactsSecrets)
{
    Config::UserConfig cfg;
    cfg.server.apiKey     = "secret";
    cfg.server.sslKeyFile = "";        // empty stays empty
    cfg.api.apiKey        = "topsecret";
    ON_CALL(mockConfig(), getConfigSnapshot()).WillByDefault(Return(cfg));

    httplib::Client cli(kHost, kPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res = cli.Get("/config/app");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 200);

    const auto body = nlohmann::json::parse(res->body, nullptr, false);
    ASSERT_TRUE(body.contains("server"));
    ASSERT_TRUE(body.contains("api"));
    EXPECT_EQ(body["server"]["apiKey"].get<std::string>(), "***REDACTED***");
    EXPECT_EQ(body["api"]["apiKey"].get<std::string>(), "***REDACTED***");
    EXPECT_EQ(body["server"]["sslKeyFile"].get<std::string>(), "");
}

/**
 * @brief GET /config/app with auth enabled and no key returns 401 and must not
 * read the config snapshot.
 */
TEST_F(ApiRoutesTest, GetConfigApp_AuthOn_MissingKey_Returns401)
{
    httplib::Server authSvr;
    constexpr int kAuthPort = 18197;
    ASSERT_TRUE(authSvr.bind_to_port(kHost, kAuthPort));

    ControlApiDeps deps = makeDeps(/*requireKey=*/true, /*key=*/"secret");
    registerControlApiRoutes(authSvr, deps);

    std::thread t([&authSvr] { authSvr.listen_after_bind(); });
    authSvr.wait_until_ready();

    EXPECT_CALL(mockConfig(), getConfigSnapshot()).Times(0);

    httplib::Client cli(kHost, kAuthPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res = cli.Get("/config/app");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 401);

    authSvr.stop();
    t.join();
}

// ---------------------------------------------------------------------------
// GET /config/models — full preset list (issue #108)
// ---------------------------------------------------------------------------

namespace {

/// Build a preset carrying an hfToken so redaction can be asserted.
Config::ModelPreset makeTokenPreset(const std::string &name,
                                    const std::string &hfToken)
{
    Config::ModelPreset p = makeTestPreset(name, "/models/" + name + ".gguf", 4096);
    p.load.hfToken        = hfToken;
    return p;
}

} // namespace

/**
 * @brief GET /config/models returns full presets and redacts a non-empty
 * load.hfToken to the sentinel while leaving an empty one empty.
 */
TEST_F(ApiRoutesTest, GetConfigModels_ReturnsFullAndRedactsToken)
{
    ON_CALL(mockModels(), getModelNames())
        .WillByDefault(Return(std::vector<std::string>{"alpha", "beta"}));
    ON_CALL(mockModels(), getPreset("alpha"))
        .WillByDefault(Return(std::optional<Config::ModelPreset>{
            makeTokenPreset("alpha", "hf_token_value")}));
    ON_CALL(mockModels(), getPreset("beta"))
        .WillByDefault(Return(std::optional<Config::ModelPreset>{
            makeTokenPreset("beta", "")}));

    httplib::Client cli(kHost, kPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res = cli.Get("/config/models");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 200);

    const auto body = nlohmann::json::parse(res->body, nullptr, false);
    ASSERT_TRUE(body.contains("models"));
    ASSERT_EQ(body["models"].size(), 2u);
    // Full ModelPreset shape: name, model, load{}, inference{}.
    EXPECT_EQ(body["models"][0]["name"].get<std::string>(), "alpha");
    EXPECT_TRUE(body["models"][0].contains("inference"));
    EXPECT_EQ(body["models"][0]["load"]["hfToken"].get<std::string>(),
              "***REDACTED***");
    EXPECT_EQ(body["models"][1]["load"]["hfToken"].get<std::string>(), "");
}

/**
 * @brief GET /config/models with auth enabled and no key returns 401.
 */
TEST_F(ApiRoutesTest, GetConfigModels_AuthOn_MissingKey_Returns401)
{
    httplib::Server authSvr;
    constexpr int kAuthPort = 18198;
    ASSERT_TRUE(authSvr.bind_to_port(kHost, kAuthPort));

    ControlApiDeps deps = makeDeps(/*requireKey=*/true, /*key=*/"secret");
    registerControlApiRoutes(authSvr, deps);

    std::thread t([&authSvr] { authSvr.listen_after_bind(); });
    authSvr.wait_until_ready();

    EXPECT_CALL(mockModels(), getModelNames()).Times(0);

    httplib::Client cli(kHost, kAuthPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res = cli.Get("/config/models");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 401);

    authSvr.stop();
    t.join();
}

// ---------------------------------------------------------------------------
// GET /config/models/{name} — single preset (issue #108)
// ---------------------------------------------------------------------------

/**
 * @brief GET /config/models/{name} returns 200 with the full preset and a
 * redacted hfToken when the section exists.
 */
TEST_F(ApiRoutesTest, GetConfigModelByName_Known_Returns200)
{
    ON_CALL(mockModels(), getPreset("alpha"))
        .WillByDefault(Return(std::optional<Config::ModelPreset>{
            makeTokenPreset("alpha", "hf_token_value")}));

    httplib::Client cli(kHost, kPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res = cli.Get("/config/models/alpha");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 200);

    const auto body = nlohmann::json::parse(res->body, nullptr, false);
    EXPECT_EQ(body["name"].get<std::string>(), "alpha");
    EXPECT_EQ(body["load"]["hfToken"].get<std::string>(), "***REDACTED***");
}

/**
 * @brief GET /config/models/{name} returns 404 when the section is unknown.
 */
TEST_F(ApiRoutesTest, GetConfigModelByName_Unknown_Returns404)
{
    ON_CALL(mockModels(), getPreset("missing"))
        .WillByDefault(Return(std::optional<Config::ModelPreset>{}));

    httplib::Client cli(kHost, kPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res = cli.Get("/config/models/missing");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 404);

    const auto body = nlohmann::json::parse(res->body, nullptr, false);
    EXPECT_TRUE(body["error"].get<std::string>().find("unknown model preset") !=
                std::string::npos);
}

// ---------------------------------------------------------------------------
// PUT /config/models/{name} — upsert preset (issue #108)
// ---------------------------------------------------------------------------

/**
 * @brief PUT /config/models/{name} forces the section name from the path (so a
 * body name cannot rename) and persists via savePreset; returns 200.
 */
TEST_F(ApiRoutesTest, PutConfigModel_Valid_PersistsWithPathName)
{
    // Body deliberately names the preset "WRONG" to prove the path wins.
    nlohmann::json bodyJson = makeTestPreset("WRONG", "/models/x.gguf", 2048);

    EXPECT_CALL(mockModels(),
                savePreset(::testing::Field(&Config::ModelPreset::name, "alpha")))
        .Times(1)
        .WillOnce(Return(true));

    httplib::Client cli(kHost, kPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res =
        cli.Put("/config/models/alpha", bodyJson.dump(), "application/json");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 200);

    const auto body = nlohmann::json::parse(res->body, nullptr, false);
    EXPECT_EQ(body["status"].get<std::string>(), "saved");
    EXPECT_EQ(body["model"].get<std::string>(), "alpha");
}

/**
 * @brief PUT /config/models/{name} with a malformed JSON body returns 400 and
 * must not call savePreset.
 */
TEST_F(ApiRoutesTest, PutConfigModel_MalformedBody_Returns400)
{
    EXPECT_CALL(mockModels(), savePreset(_)).Times(0);

    httplib::Client cli(kHost, kPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res = cli.Put("/config/models/alpha", "{not json", "application/json");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 400);
}

/**
 * @brief PUT /config/models/{name} when savePreset returns false yields 500.
 */
TEST_F(ApiRoutesTest, PutConfigModel_SaveFails_Returns500)
{
    ON_CALL(mockModels(), savePreset(_)).WillByDefault(Return(false));

    nlohmann::json bodyJson = makeTestPreset("alpha", "/models/x.gguf", 2048);

    httplib::Client cli(kHost, kPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res =
        cli.Put("/config/models/alpha", bodyJson.dump(), "application/json");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 500);

    const auto body = nlohmann::json::parse(res->body, nullptr, false);
    EXPECT_EQ(body["status"].get<std::string>(), "failed");
}

/**
 * @brief PUT /config/models/{name} with auth enabled and wrong key returns 401
 * and must not call savePreset.
 */
TEST_F(ApiRoutesTest, PutConfigModel_AuthOn_WrongKey_Returns401)
{
    httplib::Server authSvr;
    constexpr int kAuthPort = 18199;
    ASSERT_TRUE(authSvr.bind_to_port(kHost, kAuthPort));

    ControlApiDeps deps = makeDeps(/*requireKey=*/true, /*key=*/"secret");
    registerControlApiRoutes(authSvr, deps);

    std::thread t([&authSvr] { authSvr.listen_after_bind(); });
    authSvr.wait_until_ready();

    EXPECT_CALL(mockModels(), savePreset(_)).Times(0);

    nlohmann::json bodyJson = makeTestPreset("alpha", "/models/x.gguf", 2048);

    httplib::Client cli(kHost, kAuthPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    httplib::Headers headers;
    headers.emplace("Authorization", "Bearer wrong");
    auto res = cli.Put("/config/models/alpha", headers, bodyJson.dump(),
                       "application/json");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 401);

    authSvr.stop();
    t.join();
}

// ---------------------------------------------------------------------------
// GET /batches and GET /batches/{name}  (issue #111)
// ---------------------------------------------------------------------------

TEST_F(ApiRoutesTest, GetBatches_ReturnsNames)
{
    ON_CALL(mockBatches(), getBatchNames())
        .WillByDefault(Return(std::vector<std::string>{"smoke", "prod"}));

    httplib::Client cli(kHost, kPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res = cli.Get("/batches");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 200);

    const auto body = nlohmann::json::parse(res->body, nullptr, false);
    EXPECT_EQ(body["batches"],
              (std::vector<std::string>{"smoke", "prod"}));
}

TEST_F(ApiRoutesTest, GetBatch_Known_ReturnsPresets)
{
    ON_CALL(mockBatches(), getBatch("smoke"))
        .WillByDefault(Return(Batch{"smoke", {"a", "b"}}));

    httplib::Client cli(kHost, kPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res = cli.Get("/batches/smoke");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 200);

    const auto body = nlohmann::json::parse(res->body, nullptr, false);
    EXPECT_EQ(body["batch"].get<std::string>(), "smoke");
    EXPECT_EQ(body["presets"], (std::vector<std::string>{"a", "b"}));
}

TEST_F(ApiRoutesTest, GetBatch_Unknown_Returns404)
{
    ON_CALL(mockBatches(), getBatch("ghost"))
        .WillByDefault(Return(std::nullopt));

    httplib::Client cli(kHost, kPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res = cli.Get("/batches/ghost");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 404);
}

// ---------------------------------------------------------------------------
// POST /batches/load  (issue #111)
// ---------------------------------------------------------------------------

TEST_F(ApiRoutesTest, PostBatchesLoad_LoadsEachPreset)
{
    ON_CALL(mockBatches(), getBatch("smoke"))
        .WillByDefault(Return(Batch{"smoke", {"a", "b"}}));
    ON_CALL(mockModels(), getModelNames())
        .WillByDefault(Return(std::vector<std::string>{"a", "b"}));

    {
        InSequence seq;
        EXPECT_CALL(mockTracker(), requestLoad(std::string("a")));
        EXPECT_CALL(mockServer(), loadModel("a")).WillOnce(Return(true));
        EXPECT_CALL(mockTracker(), requestLoad(std::string("b")));
        EXPECT_CALL(mockServer(), loadModel("b")).WillOnce(Return(true));
    }

    httplib::Client cli(kHost, kPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res =
        cli.Post("/batches/load", R"({"batch":"smoke"})", "application/json");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 200);

    const auto body = nlohmann::json::parse(res->body, nullptr, false);
    EXPECT_EQ(body["loaded"], (std::vector<std::string>{"a", "b"}));
}

TEST_F(ApiRoutesTest, PostBatchesLoad_SkipsUnknownPreset)
{
    ON_CALL(mockBatches(), getBatch("smoke"))
        .WillByDefault(Return(Batch{"smoke", {"a", "ghost"}}));
    ON_CALL(mockModels(), getModelNames())
        .WillByDefault(Return(std::vector<std::string>{"a"})); // ghost absent

    EXPECT_CALL(mockServer(), loadModel("a")).WillOnce(Return(true));
    EXPECT_CALL(mockServer(), loadModel("ghost")).Times(0);

    httplib::Client cli(kHost, kPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res =
        cli.Post("/batches/load", R"({"batch":"smoke"})", "application/json");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 200);

    const auto body = nlohmann::json::parse(res->body, nullptr, false);
    EXPECT_EQ(body["loaded"], (std::vector<std::string>{"a"}));
    EXPECT_EQ(body["skipped"], (std::vector<std::string>{"ghost"}));
}

TEST_F(ApiRoutesTest, PostBatchesLoad_UnknownBatch_Returns400)
{
    ON_CALL(mockBatches(), getBatch("ghost"))
        .WillByDefault(Return(std::nullopt));
    EXPECT_CALL(mockServer(), loadModel(_)).Times(0);

    httplib::Client cli(kHost, kPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res =
        cli.Post("/batches/load", R"({"batch":"ghost"})", "application/json");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 400);
}

TEST_F(ApiRoutesTest, PostBatchesLoad_MissingField_Returns400)
{
    EXPECT_CALL(mockServer(), loadModel(_)).Times(0);

    httplib::Client cli(kHost, kPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res = cli.Post("/batches/load", R"({})", "application/json");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 400);
}

// ---------------------------------------------------------------------------
// POST /batches/unload  (issue #111)
// ---------------------------------------------------------------------------

TEST_F(ApiRoutesTest, PostBatchesUnload_Named_UnloadsEach)
{
    ON_CALL(mockBatches(), getBatch("smoke"))
        .WillByDefault(Return(Batch{"smoke", {"a", "b"}}));

    EXPECT_CALL(mockTracker(), requestUnload(std::string("a")));
    EXPECT_CALL(mockServer(), unloadModel("a")).WillOnce(Return(true));
    EXPECT_CALL(mockTracker(), requestUnload(std::string("b")));
    EXPECT_CALL(mockServer(), unloadModel("b")).WillOnce(Return(true));

    httplib::Client cli(kHost, kPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res =
        cli.Post("/batches/unload", R"({"batch":"smoke"})", "application/json");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 200);

    const auto body = nlohmann::json::parse(res->body, nullptr, false);
    EXPECT_EQ(body["scope"].get<std::string>(), "batch");
    EXPECT_EQ(body["unloaded"], (std::vector<std::string>{"a", "b"}));
}

TEST_F(ApiRoutesTest, PostBatchesUnload_NoBody_UnloadsAll)
{
    EXPECT_CALL(mockTracker(), requestUnloadAll());
    EXPECT_CALL(mockServer(), unloadAllModels()).WillOnce(Return(true));
    EXPECT_CALL(mockServer(), unloadModel(_)).Times(0);

    httplib::Client cli(kHost, kPort);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    auto res = cli.Post("/batches/unload", "", "application/json");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 200);

    const auto body = nlohmann::json::parse(res->body, nullptr, false);
    EXPECT_EQ(body["scope"].get<std::string>(), "all");
}

TEST_F(ApiRoutesTest, PostBatchesLoad_Unauthorized_Returns401)
{
    // Spin up a key-required server on a distinct port.
    httplib::Server authSvr;
    ASSERT_TRUE(authSvr.bind_to_port(kHost, kPort + 1));
    registerControlApiRoutes(authSvr, makeDeps(/*requireKey=*/true,
                                               /*key=*/"secret"));
    std::thread t([&] { authSvr.listen_after_bind(); });
    authSvr.wait_until_ready();

    EXPECT_CALL(mockServer(), loadModel(_)).Times(0);

    httplib::Client cli(kHost, kPort + 1);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);
    httplib::Headers headers;
    headers.emplace("Authorization", "Bearer wrong");
    auto res = cli.Post("/batches/load", headers, R"({"batch":"smoke"})",
                        "application/json");
    ASSERT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res->status, 401);

    authSvr.stop();
    t.join();
}
