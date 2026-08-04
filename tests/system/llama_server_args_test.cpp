#include <gtest/gtest.h>
#include "system/llamaServerProcess.h"
#include "config/config.h"
#include <algorithm>
#include <vector>
#include <string>

using namespace Config;

// =============================================================================
// LlamaServerProcess buildCommandArgs Tests
//
// Router mode: buildCommandArgs emits only --models-preset and server flags.
// Per-model load/inference settings come from models.ini via the load API and
// are intentionally NOT passed on the command line, so there are no -ngl/-c/
// --temp/etc. assertions here.
// =============================================================================

TEST(LlamaServerProcess, BuildCommandArgsStartsWithLlamaServer) {
    ServerSettings server;

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    ASSERT_FALSE(args.empty());
    EXPECT_EQ(args[0], "llama-server");
}

TEST(LlamaServerProcess, BuildCommandArgsModelPathNotPassedAsModelFlag) {
    ServerSettings server;

    // Router mode loads models dynamically; the modelPath param is unused.
    auto args = LlamaServerProcess::buildCommandArgs("/path/to/model.gguf", server);

    ASSERT_FALSE(args.empty());
    EXPECT_EQ(args[0], "llama-server");
    // -m should NOT be present in router mode
    for (size_t i = 1; i < args.size(); i++) {
        EXPECT_NE(args[i], "-m");
    }
}

TEST(LlamaServerProcess, BuildCommandArgsEmptyModelPath) {
    ServerSettings server;

    auto args = LlamaServerProcess::buildCommandArgs("", server);

    ASSERT_FALSE(args.empty());
    EXPECT_EQ(args[0], "llama-server");
    // -m should not be present
    for (size_t i = 1; i < args.size(); i++) {
        EXPECT_NE(args[i], "-m");
    }
}

TEST(LlamaServerProcess, BuildCommandArgsOmitsLoadAndInferenceFlags) {
    ServerSettings server;

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    // None of the per-model load/inference flags should be emitted in router mode.
    static const std::vector<std::string> forbidden = {
        "-ngl", "-c", "-b", "-ub", "-np", "-t", "-tb", "-fa", "-ctk", "-ctv",
        "-sm", "--tensor-split", "-mg", "--lora", "-mm", "-md", "--spec-draft-n-max",
        "--spec-type", "--chat-template", "--reasoning-format", "--fit",
        "-s", "-n", "--temp", "--top-p", "--top-k", "--min-p",
        "--repeat-penalty", "--presence-penalty", "--frequency-penalty",
        // Phase 4 inference params (section D)
        "--ignore-eos", "-l", "--logit-bias",
        "--adaptive-target", "--adaptive-decay",
        "--grammar-file", "--json-schema-file",
        "--sampler-seq", "--dry-sequence-breaker",
        "--backend-sampling",
        // Phase 3 load params (section C)
        "--n-cpu-moe", "-ncmoe", "--cpu-moe", "--override-tensor", "-ot",
        "--rope-scaling", "--rope-scale", "--rope-freq-base", "--rope-freq-scale",
        "--yarn-orig-ctx", "--yarn-ext-factor", "--yarn-attn-factor",
        "--yarn-beta-slow", "--yarn-beta-fast", "--swa-full", "--keep",
        "--numa", "--fit-target", "--fit-ctx", "--check-tensors", "--override-kv",
        "--lora-scaled", "--control-vector", "--control-vector-scaled",
        "--spec-draft-n-min", "--spec-draft-p-min", "--spec-draft-p-split",
        "--cpu-moe-draft",
    };
    for (const auto &flag : forbidden) {
        EXPECT_EQ(std::find(args.begin(), args.end(), flag), args.end())
            << "Unexpected load/inference flag emitted in router mode: " << flag;
    }
}

// =============================================================================
// Server flag Tests
// =============================================================================

TEST(LlamaServerProcess, BuildCommandArgsWithServerHost) {
    ServerSettings server;
    server.host = "127.0.0.1";

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    auto it = std::find(args.begin(), args.end(), "--host");
    ASSERT_NE(it, args.end());
    ++it;
    EXPECT_EQ(*it, "127.0.0.1");
}

TEST(LlamaServerProcess, BuildCommandArgsWithServerPort) {
    ServerSettings server;
    server.port = 8080;

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    auto it = std::find(args.begin(), args.end(), "--port");
    ASSERT_NE(it, args.end());
    ++it;
    EXPECT_EQ(*it, "8080");
}

TEST(LlamaServerProcess, BuildCommandArgsWithApiKeyUsesKeyFile) {
    ServerSettings server;
    server.apiKey = "secret-key";

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    // Security: the key must never appear inline on the command line
    // (visible to every local process); it is written to a private file and
    // passed via --api-key-file instead.
    EXPECT_EQ(std::find(args.begin(), args.end(), "secret-key"), args.end());
    EXPECT_EQ(std::find(args.begin(), args.end(), "--api-key"), args.end());

    auto it = std::find(args.begin(), args.end(), "--api-key-file");
    ASSERT_NE(it, args.end());
    ++it;
    EXPECT_NE(it->find("llama-api-key.txt"), std::string::npos);
}

TEST(LlamaServerProcess, BuildCommandArgsExplicitKeyFileWinsOverApiKey) {
    ServerSettings server;
    server.apiKey = "secret-key";
    server.apiKeyFile = "/path/to/keyfile";

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    auto it = std::find(args.begin(), args.end(), "--api-key-file");
    ASSERT_NE(it, args.end());
    ++it;
    EXPECT_EQ(*it, "/path/to/keyfile");
    EXPECT_EQ(std::find(args.begin(), args.end(), "secret-key"), args.end());
}

TEST(LlamaServerProcess, BuildCommandArgsWithApiKeyFile) {
    ServerSettings server;
    server.apiKeyFile = "/path/to/keyfile";

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    auto it = std::find(args.begin(), args.end(), "--api-key-file");
    ASSERT_NE(it, args.end());
    ++it;
    EXPECT_EQ(*it, "/path/to/keyfile");
}

TEST(LlamaServerProcess, BuildCommandArgsWithTimeout) {
    ServerSettings server;
    server.timeout = 300;

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    auto it = std::find(args.begin(), args.end(), "--timeout");
    ASSERT_NE(it, args.end());
    ++it;
    EXPECT_EQ(*it, "300");
}

TEST(LlamaServerProcess, BuildCommandArgsWithThreadsHttp) {
    ServerSettings server;
    server.threadsHttp = 4;

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    auto it = std::find(args.begin(), args.end(), "--threads-http");
    ASSERT_NE(it, args.end());
    ++it;
    EXPECT_EQ(*it, "4");
}

TEST(LlamaServerProcess, BuildCommandArgsWithReusePort) {
    ServerSettings server;
    server.reusePort = true;

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    ASSERT_NE(std::find(args.begin(), args.end(), "--reuse-port"), args.end());
}

TEST(LlamaServerProcess, BuildCommandArgsWithSslKeyFile) {
    ServerSettings server;
    server.sslKeyFile = "/path/to/key.pem";

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    auto it = std::find(args.begin(), args.end(), "--ssl-key-file");
    ASSERT_NE(it, args.end());
    ++it;
    EXPECT_EQ(*it, "/path/to/key.pem");
}

TEST(LlamaServerProcess, BuildCommandArgsWithSslCertFile) {
    ServerSettings server;
    server.sslCertFile = "/path/to/cert.pem";

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    auto it = std::find(args.begin(), args.end(), "--ssl-cert-file");
    ASSERT_NE(it, args.end());
    ++it;
    EXPECT_EQ(*it, "/path/to/cert.pem");
}

TEST(LlamaServerProcess, BuildCommandArgsWithStaticPath) {
    ServerSettings server;
    server.path = "/var/www/html";

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    auto it = std::find(args.begin(), args.end(), "--path");
    ASSERT_NE(it, args.end());
    ++it;
    EXPECT_EQ(*it, "/var/www/html");
}

TEST(LlamaServerProcess, BuildCommandArgsWithApiPrefix) {
    ServerSettings server;
    server.apiPrefix = "/api";

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    auto it = std::find(args.begin(), args.end(), "--api-prefix");
    ASSERT_NE(it, args.end());
    ++it;
    EXPECT_EQ(*it, "/api");
}

TEST(LlamaServerProcess, BuildCommandArgsWithMediaPath) {
    ServerSettings server;
    server.mediaPath = "/path/to/media";

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    auto it = std::find(args.begin(), args.end(), "--media-path");
    ASSERT_NE(it, args.end());
    ++it;
    EXPECT_EQ(*it, "/path/to/media");
}

TEST(LlamaServerProcess, BuildCommandArgsWithAlias) {
    ServerSettings server;
    server.alias = "test-server";

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    auto it = std::find(args.begin(), args.end(), "--alias");
    ASSERT_NE(it, args.end());
    ++it;
    EXPECT_EQ(*it, "test-server");
}

TEST(LlamaServerProcess, BuildCommandArgsWithNoUi) {
    ServerSettings server;
    server.ui = false;

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    ASSERT_NE(std::find(args.begin(), args.end(), "--no-ui"), args.end());
}

TEST(LlamaServerProcess, BuildCommandArgsWithUiConfig) {
    ServerSettings server;
    server.uiConfig = R"({"theme":"dark"})";

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    auto it = std::find(args.begin(), args.end(), "--ui-config");
    ASSERT_NE(it, args.end());
    ++it;
    EXPECT_EQ(*it, R"({"theme":"dark"})");
}

TEST(LlamaServerProcess, BuildCommandArgsWithUiConfigFile) {
    ServerSettings server;
    server.uiConfigFile = "/path/to/config.json";

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    auto it = std::find(args.begin(), args.end(), "--ui-config-file");
    ASSERT_NE(it, args.end());
    ++it;
    EXPECT_EQ(*it, "/path/to/config.json");
}

TEST(LlamaServerProcess, BuildCommandArgsWithNoUiMcpProxy) {
    ServerSettings server;
    server.uiMcpProxy = false;

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    ASSERT_NE(std::find(args.begin(), args.end(), "--no-ui-mcp-proxy"), args.end());
}

// --- Section A: server router params ---

TEST(LlamaServerProcess, BuildCommandArgsEmitsModelsMax) {
    ServerSettings server;
    server.modelsMax = 7;

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    auto it = std::find(args.begin(), args.end(), "--models-max");
    ASSERT_NE(it, args.end());
    ++it;
    EXPECT_EQ(*it, "7");
}

TEST(LlamaServerProcess, BuildCommandArgsDefaultEmitsNoModelsAutoload) {
    ServerSettings server; // modelsAutoload defaults false

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    EXPECT_NE(std::find(args.begin(), args.end(), "--no-models-autoload"),
              args.end());
    EXPECT_EQ(std::find(args.begin(), args.end(), "--models-autoload"),
              args.end());
}

TEST(LlamaServerProcess, BuildCommandArgsModelsAutoloadOptIn) {
    ServerSettings server;
    server.modelsAutoload = true;

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    EXPECT_NE(std::find(args.begin(), args.end(), "--models-autoload"),
              args.end());
    EXPECT_EQ(std::find(args.begin(), args.end(), "--no-models-autoload"),
              args.end());
}

TEST(LlamaServerProcess, BuildCommandArgsEmitsCheckpointMinStep) {
    ServerSettings server;
    server.checkpointMinStep = 4096;

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    auto it = std::find(args.begin(), args.end(), "--checkpoint-min-step");
    ASSERT_NE(it, args.end());
    ++it;
    EXPECT_EQ(*it, "4096");
}

TEST(LlamaServerProcess, BuildCommandArgsEmitsNoCacheIdleSlots) {
    ServerSettings server;
    server.cacheIdleSlots = false;

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    EXPECT_NE(std::find(args.begin(), args.end(), "--no-cache-idle-slots"),
              args.end());
}

TEST(LlamaServerProcess, BuildCommandArgsEmitsPoolingWhenSet) {
    ServerSettings server;
    server.pooling = "mean";

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    auto it = std::find(args.begin(), args.end(), "--pooling");
    ASSERT_NE(it, args.end());
    ++it;
    EXPECT_EQ(*it, "mean");
}

TEST(LlamaServerProcess, BuildCommandArgsOmitsPoolingWhenEmpty) {
    ServerSettings server; // pooling empty by default

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    EXPECT_EQ(std::find(args.begin(), args.end(), "--pooling"), args.end());
}

TEST(LlamaServerProcess, BuildCommandArgsOmitsEmbdNormalizeWhenNegativeOne) {
    ServerSettings server; // embdNormalize == -1

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    EXPECT_EQ(std::find(args.begin(), args.end(), "--embd-normalize"),
              args.end());
}

TEST(LlamaServerProcess, BuildCommandArgsEmitsReasoningAndBudget) {
    ServerSettings server;
    server.reasoning = "on";
    server.reasoningBudget = 256;

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    auto rit = std::find(args.begin(), args.end(), "--reasoning");
    ASSERT_NE(rit, args.end());
    EXPECT_EQ(*(rit + 1), "on");
    auto bit = std::find(args.begin(), args.end(), "--reasoning-budget");
    ASSERT_NE(bit, args.end());
    EXPECT_EQ(*(bit + 1), "256");
}

TEST(LlamaServerProcess, BuildCommandArgsOmitsReasoningBudgetWhenNegativeOne) {
    ServerSettings server; // reasoningBudget == -1

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    EXPECT_EQ(std::find(args.begin(), args.end(), "--reasoning-budget"),
              args.end());
}

TEST(LlamaServerProcess, BuildCommandArgsWithTools) {
    ServerSettings server;
    server.tools = "all";

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    auto it = std::find(args.begin(), args.end(), "--tools");
    ASSERT_NE(it, args.end());
    ++it;
    EXPECT_EQ(*it, "all");
}

TEST(LlamaServerProcess, BuildCommandArgsWithEmbedding) {
    ServerSettings server;
    server.embedding = true;

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    ASSERT_NE(std::find(args.begin(), args.end(), "--embedding"), args.end());
}

TEST(LlamaServerProcess, BuildCommandArgsWithReranking) {
    ServerSettings server;
    server.reranking = true;

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    ASSERT_NE(std::find(args.begin(), args.end(), "--rerank"), args.end());
}

TEST(LlamaServerProcess, BuildCommandArgsWithNoContBatching) {
    ServerSettings server;
    server.contBatching = false;

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    ASSERT_NE(std::find(args.begin(), args.end(), "--no-cont-batching"), args.end());
}

TEST(LlamaServerProcess, BuildCommandArgsWithNoCachePrompt) {
    ServerSettings server;
    server.cachePrompt = false;

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    ASSERT_NE(std::find(args.begin(), args.end(), "--no-cache-prompt"), args.end());
}

TEST(LlamaServerProcess, BuildCommandArgsWithCacheReuse) {
    ServerSettings server;
    server.cacheReuse = 10;

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    auto it = std::find(args.begin(), args.end(), "--cache-reuse");
    ASSERT_NE(it, args.end());
    ++it;
    EXPECT_EQ(*it, "10");
}

TEST(LlamaServerProcess, BuildCommandArgsAlwaysEmitsCacheRam) {
    ServerSettings server; // default 8192

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    auto it = std::find(args.begin(), args.end(), "--cache-ram");
    ASSERT_NE(it, args.end());
    ++it;
    EXPECT_EQ(*it, "8192");
}

TEST(LlamaServerProcess, BuildCommandArgsCacheRamDisableValue) {
    ServerSettings server;
    server.cacheRam = 0; // disable is a meaningful value, must still emit

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    auto it = std::find(args.begin(), args.end(), "--cache-ram");
    ASSERT_NE(it, args.end());
    ++it;
    EXPECT_EQ(*it, "0");
}

TEST(LlamaServerProcess, BuildCommandArgsAlwaysEmitsCtxCheckpoints) {
    ServerSettings server; // default 32

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    auto it = std::find(args.begin(), args.end(), "--ctx-checkpoints");
    ASSERT_NE(it, args.end());
    ++it;
    EXPECT_EQ(*it, "32");
}

TEST(LlamaServerProcess, BuildCommandArgsCtxCheckpointsDisableValue) {
    ServerSettings server;
    server.ctxCheckpoints = 0; // disable is a meaningful value, must still emit

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    auto it = std::find(args.begin(), args.end(), "--ctx-checkpoints");
    ASSERT_NE(it, args.end());
    ++it;
    EXPECT_EQ(*it, "0");
}

TEST(LlamaServerProcess, BuildCommandArgsWithContextShift) {
    ServerSettings server;
    server.contextShift = true;

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    ASSERT_NE(std::find(args.begin(), args.end(), "--context-shift"), args.end());
}

TEST(LlamaServerProcess, BuildCommandArgsWithNoWarmup) {
    ServerSettings server;
    server.warmup = false;

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    ASSERT_NE(std::find(args.begin(), args.end(), "--no-warmup"), args.end());
}

TEST(LlamaServerProcess, BuildCommandArgsWithNoJinja) {
    ServerSettings server;
    server.jinja = false;

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    ASSERT_NE(std::find(args.begin(), args.end(), "--no-jinja"), args.end());
}

TEST(LlamaServerProcess, BuildCommandArgsWithNoPrefillAssistant) {
    ServerSettings server;
    server.prefillAssistant = false;

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    ASSERT_NE(std::find(args.begin(), args.end(), "--no-prefill-assistant"), args.end());
}

TEST(LlamaServerProcess, BuildCommandArgsWithSlotPromptSimilarity) {
    ServerSettings server;
    server.slotPromptSimilarity = 0.5;

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    auto it = std::find(args.begin(), args.end(), "--slot-prompt-similarity");
    ASSERT_NE(it, args.end());
    ++it;
    EXPECT_EQ(*it, "0.500000");
}

TEST(LlamaServerProcess, BuildCommandArgsWithSleepIdleSeconds) {
    ServerSettings server;
    server.sleepIdleSeconds = 60;

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    auto it = std::find(args.begin(), args.end(), "--sleep-idle-seconds");
    ASSERT_NE(it, args.end());
    ++it;
    EXPECT_EQ(*it, "60");
}

TEST(LlamaServerProcess, BuildCommandArgsWithMetrics) {
    ServerSettings server;
    server.metrics = true;

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    ASSERT_NE(std::find(args.begin(), args.end(), "--metrics"), args.end());
}

TEST(LlamaServerProcess, BuildCommandArgsWithProps) {
    ServerSettings server;
    server.props = true;

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    ASSERT_NE(std::find(args.begin(), args.end(), "--props"), args.end());
}

TEST(LlamaServerProcess, BuildCommandArgsWithNoSlots) {
    ServerSettings server;
    server.slots = false;

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    ASSERT_NE(std::find(args.begin(), args.end(), "--no-slots"), args.end());
}

TEST(LlamaServerProcess, BuildCommandArgsWithSlotSavePath) {
    ServerSettings server;
    server.slotSavePath = "/path/to/slots";

    auto args = LlamaServerProcess::buildCommandArgs("model.gguf", server);

    auto it = std::find(args.begin(), args.end(), "--slot-save-path");
    ASSERT_NE(it, args.end());
    ++it;
    EXPECT_EQ(*it, "/path/to/slots");
}

TEST(LlamaServerProcess, BuildCommandArgsServerOnly) {
    ServerSettings server;
    server.host = "0.0.0.0";
    server.port = 9090;

    auto args = LlamaServerProcess::buildCommandArgs("test.gguf", server);

    EXPECT_NE(std::find(args.begin(), args.end(), "--host"), args.end());
    EXPECT_NE(std::find(args.begin(), args.end(), "--port"), args.end());
    // -m should NOT be present in router mode
    EXPECT_EQ(std::find(args.begin(), args.end(), "-m"), args.end());
}

// =============================================================================
// getLogPath Tests
// =============================================================================

TEST(LlamaServerProcess, GetLogPathReturnsValidPath) {
    std::string logPath = LlamaServerProcess::getLogPath();

    // Should end with llama-server.log
    EXPECT_TRUE(logPath.find("llama-server.log") != std::string::npos);
}

TEST(LlamaServerProcess, GetLogPathContainsLogsDir) {
    std::string logPath = LlamaServerProcess::getLogPath();

    // Should contain "logs" directory
    EXPECT_TRUE(logPath.find("/logs/") != std::string::npos ||
                logPath.find("\\logs\\") != std::string::npos);
}
