#include <gtest/gtest.h>
#include "config/config.h"
#include "config/configManager.h"
#include "json.hpp"
#include <fstream>
#include <filesystem>

using namespace Config;

// Helper to convert config to JSON and back
template <typename T>
nlohmann::json serializeRoundtrip(const T& obj) {
    nlohmann::json j;
    to_json(j, obj);
    return j;
}

template <typename T>
T deserializeRoundtrip(const nlohmann::json& j) {
    T obj;
    from_json(j, obj);
    return obj;
}

// =============================================================================
// ServerSettings Tests
// =============================================================================

TEST(ConfigSerialization, ServerSettings_DefaultRoundtrip) {
    ServerSettings original;
    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<ServerSettings>(j);

    EXPECT_EQ(original.host, restored.host);
    EXPECT_EQ(original.port, restored.port);
    EXPECT_EQ(original.apiKey, restored.apiKey);
    EXPECT_EQ(original.timeout, restored.timeout);
    EXPECT_EQ(original.threadsHttp, restored.threadsHttp);
    EXPECT_EQ(original.cacheRam, restored.cacheRam);
    EXPECT_EQ(original.ctxCheckpoints, restored.ctxCheckpoints);
    EXPECT_EQ(original.ui, restored.ui);
    EXPECT_EQ(original.embedding, restored.embedding);
    EXPECT_EQ(original.reusePort, restored.reusePort);
    EXPECT_EQ(original.uiMcpProxy, restored.uiMcpProxy);
    EXPECT_EQ(original.tools, restored.tools);
}

TEST(ConfigSerialization, ServerSettings_RouterParamsRoundTrip) {
    ServerSettings original;
    original.modelsMax = 8;
    original.modelsAutoload = true;
    original.checkpointMinStep = 2048;
    original.cacheIdleSlots = false;
    original.pooling = "cls";
    original.embdNormalize = 2;
    original.reasoning = "off";
    original.reasoningBudget = 512;

    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<ServerSettings>(j);

    EXPECT_EQ(restored.modelsMax, 8);
    EXPECT_EQ(restored.modelsAutoload, true);
    EXPECT_EQ(restored.checkpointMinStep, 2048);
    EXPECT_EQ(restored.cacheIdleSlots, false);
    EXPECT_EQ(restored.pooling, "cls");
    EXPECT_EQ(restored.embdNormalize, 2);
    EXPECT_EQ(restored.reasoning, "off");
    EXPECT_EQ(restored.reasoningBudget, 512);
}

TEST(ConfigSerialization, ServerSettings_ModifiedValues) {
    ServerSettings original;
    original.host = "0.0.0.0";
    original.port = 9090;
    original.apiKey = "secret-key-123";
    original.timeout = 300;
    original.threadsHttp = 4;
    original.cacheRam = 4096;
    original.ctxCheckpoints = 16;
    original.reusePort = true;
    original.ui = false;
    original.uiConfig = R"({"theme":"dark"})";
    original.uiConfigFile = "/path/to/config.json";
    original.uiMcpProxy = true;
    original.tools = "all";
    original.embedding = true;
    original.sslKeyFile = "/path/to/key.pem";
    original.sslCertFile = "/path/to/cert.pem";
    original.path = "/var/www/html";
    original.apiPrefix = "/api";
    original.mediaPath = "/path/to/media";
    original.alias = "test-server";
    original.metrics = true;
    original.props = true;
    original.slots = false;

    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<ServerSettings>(j);

    EXPECT_EQ(restored.host, "0.0.0.0");
    EXPECT_EQ(restored.port, 9090);
    EXPECT_EQ(restored.apiKey, "secret-key-123");
    EXPECT_EQ(restored.timeout, 300);
    EXPECT_EQ(restored.threadsHttp, 4);
    EXPECT_EQ(restored.cacheRam, 4096);
    EXPECT_EQ(restored.ctxCheckpoints, 16);
    EXPECT_EQ(restored.reusePort, true);
    EXPECT_EQ(restored.ui, false);
    EXPECT_EQ(restored.uiConfig, R"({"theme":"dark"})");
    EXPECT_EQ(restored.uiConfigFile, "/path/to/config.json");
    EXPECT_EQ(restored.uiMcpProxy, true);
    EXPECT_EQ(restored.tools, "all");
    EXPECT_EQ(restored.embedding, true);
    EXPECT_EQ(restored.sslKeyFile, "/path/to/key.pem");
    EXPECT_EQ(restored.sslCertFile, "/path/to/cert.pem");
    EXPECT_EQ(restored.path, "/var/www/html");
    EXPECT_EQ(restored.apiPrefix, "/api");
    EXPECT_EQ(restored.mediaPath, "/path/to/media");
    EXPECT_EQ(restored.alias, "test-server");
    EXPECT_EQ(restored.metrics, true);
    EXPECT_EQ(restored.props, true);
    EXPECT_EQ(restored.slots, false);
}

// =============================================================================
// ApiSettings Tests
// =============================================================================

TEST(ConfigSerialization, ApiSettings_DefaultRoundtrip) {
    ApiSettings original;
    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<ApiSettings>(j);

    EXPECT_EQ(original.apiEnabled, restored.apiEnabled);
    EXPECT_EQ(original.apiHost, restored.apiHost);
    EXPECT_EQ(original.apiPort, restored.apiPort);
    EXPECT_EQ(original.apiRequireKey, restored.apiRequireKey);
    EXPECT_EQ(original.apiKey, restored.apiKey);
}

TEST(ConfigSerialization, ApiSettings_ModifiedValuesRoundtrip) {
    ApiSettings original;
    original.apiEnabled = true;
    original.apiHost = "127.0.0.1";
    original.apiPort = 9100;
    original.apiRequireKey = true;
    original.apiKey = "control-secret";

    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<ApiSettings>(j);

    EXPECT_EQ(restored.apiEnabled, true);
    EXPECT_EQ(restored.apiHost, "127.0.0.1");
    EXPECT_EQ(restored.apiPort, 9100);
    EXPECT_EQ(restored.apiRequireKey, true);
    EXPECT_EQ(restored.apiKey, "control-secret");
}

TEST(ConfigSerialization, ApiSettings_MissingKeysUseDefaults) {
    // An empty object should deserialize to default-constructed values.
    nlohmann::json j = nlohmann::json::object();
    auto restored = deserializeRoundtrip<ApiSettings>(j);
    ApiSettings defaults;

    EXPECT_EQ(restored.apiEnabled, defaults.apiEnabled);
    EXPECT_EQ(restored.apiHost, defaults.apiHost);
    EXPECT_EQ(restored.apiPort, defaults.apiPort);
    EXPECT_EQ(restored.apiRequireKey, defaults.apiRequireKey);
    EXPECT_EQ(restored.apiKey, defaults.apiKey);
}

TEST(ConfigSerialization, UserConfig_IncludesApiSettings) {
    UserConfig original;
    original.api.apiEnabled = true;
    original.api.apiPort = 9200;

    auto j = serializeRoundtrip(original);
    ASSERT_TRUE(j.contains("api"));
    auto restored = deserializeRoundtrip<UserConfig>(j);

    EXPECT_EQ(restored.api.apiEnabled, true);
    EXPECT_EQ(restored.api.apiPort, 9200);
}

// =============================================================================
// LoadSettings Tests
// =============================================================================

TEST(ConfigSerialization, LoadSettings_DefaultRoundtrip) {
    LoadSettings original;
    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<LoadSettings>(j);

    EXPECT_EQ(original.modelPath, restored.modelPath);
    EXPECT_EQ(original.ngpuLayers, restored.ngpuLayers);
    EXPECT_EQ(original.ctxSize, restored.ctxSize);
    EXPECT_EQ(original.batchSize, restored.batchSize);
    EXPECT_EQ(original.parallel, restored.parallel);
    EXPECT_EQ(original.flashAttn, restored.flashAttn);
}

TEST(ConfigSerialization, LoadSettings_ModifiedValues) {
    LoadSettings original;
    original.modelPath = "models/mistral-7b.gguf";
    original.modelUrl = "https://example.com/model.gguf";
    original.hfRepo = "mistralai/Mistral-7B-Instruct-v0.1";
    original.hfFile = "mistral-7b-instruct-v0.1-q4_k_m.gguf";
    original.ngpuLayers = 33;
    original.ctxSize = 8192;
    original.batchSize = 4096;
    original.parallel = 8;
    original.flashAttn = "off";
    original.threads = 8;
    original.lora = "adapters/english-lora.bin";
    original.mmproj = "mmproj-model.bin";

    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<LoadSettings>(j);

    EXPECT_EQ(restored.modelPath, "models/mistral-7b.gguf");
    EXPECT_EQ(restored.modelUrl, "https://example.com/model.gguf");
    EXPECT_EQ(restored.hfRepo, "mistralai/Mistral-7B-Instruct-v0.1");
    EXPECT_EQ(restored.hfFile, "mistral-7b-instruct-v0.1-q4_k_m.gguf");
    EXPECT_EQ(restored.ngpuLayers, 33);
    EXPECT_EQ(restored.ctxSize, 8192);
    EXPECT_EQ(restored.batchSize, 4096);
    EXPECT_EQ(restored.parallel, 8);
    EXPECT_EQ(restored.flashAttn, "off");
    EXPECT_EQ(restored.threads, 8);
    EXPECT_EQ(restored.lora, "adapters/english-lora.bin");
    EXPECT_EQ(restored.mmproj, "mmproj-model.bin");
}

// =============================================================================
// InferenceSettings Tests
// =============================================================================

TEST(ConfigSerialization, InferenceSettings_DefaultRoundtrip) {
    InferenceSettings original;
    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<InferenceSettings>(j);

    EXPECT_EQ(original.nPredict, restored.nPredict);
    EXPECT_EQ(original.seed, restored.seed);
    EXPECT_FLOAT_EQ(original.temperature, restored.temperature);
    EXPECT_EQ(original.topK, restored.topK);
    EXPECT_FLOAT_EQ(original.topP, restored.topP);
}

TEST(ConfigSerialization, InferenceSettings_ModifiedValues) {
    InferenceSettings original;
    original.nPredict = 512;
    original.seed = 42;
    original.temperature = 0.5;
    original.topK = 20;
    original.topP = 0.9;
    original.minP = 0.1;
    original.repeatLastN = 128;
    original.repeatPenalty = 1.1;
    original.presencePenalty = 0.5;
    original.frequencyPenalty = 0.3;
    original.mirostat = 2;
    original.mirostatLr = 0.05;
    original.mirostatEnt = 6.0;
    original.grammar = R"(root ::= "hello" | "world")";
    original.jsonSchema = R"({"type": "object"})";

    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<InferenceSettings>(j);

    EXPECT_EQ(restored.nPredict, 512);
    EXPECT_EQ(restored.seed, 42);
    EXPECT_FLOAT_EQ(restored.temperature, 0.5);
    EXPECT_EQ(restored.topK, 20);
    EXPECT_FLOAT_EQ(restored.topP, 0.9);
    EXPECT_FLOAT_EQ(restored.minP, 0.1);
    EXPECT_EQ(restored.repeatLastN, 128);
    EXPECT_FLOAT_EQ(restored.repeatPenalty, 1.1);
    EXPECT_FLOAT_EQ(restored.presencePenalty, 0.5);
    EXPECT_FLOAT_EQ(restored.frequencyPenalty, 0.3);
    EXPECT_EQ(restored.mirostat, 2);
    EXPECT_FLOAT_EQ(restored.mirostatLr, 0.05);
    EXPECT_FLOAT_EQ(restored.mirostatEnt, 6.0);
    EXPECT_EQ(restored.grammar, R"(root ::= "hello" | "world")");
    EXPECT_EQ(restored.jsonSchema, R"({"type": "object"})");
}

// =============================================================================
// UISettings Tests
// =============================================================================

TEST(ConfigSerialization, UISettings_DefaultRoundtrip) {
    UISettings original;
    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<UISettings>(j);

    EXPECT_EQ(original.theme, restored.theme);
    EXPECT_EQ(original.defaultTab, restored.defaultTab);
    EXPECT_EQ(original.showSystemPanel, restored.showSystemPanel);
    EXPECT_EQ(original.refreshRateMs, restored.refreshRateMs);
}

TEST(ConfigSerialization, UISettings_ModifiedValues) {
    UISettings original;
    original.theme = "dark";
    original.defaultTab = 1;  // int, not string
    original.showSystemPanel = false;
    original.refreshRateMs = 500;
    original.temperatureUnit = "fahrenheit";

    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<UISettings>(j);

    EXPECT_EQ(restored.theme, "dark");
    EXPECT_EQ(restored.defaultTab, 1);
    EXPECT_EQ(restored.showSystemPanel, false);
    EXPECT_EQ(restored.refreshRateMs, 500);
    EXPECT_EQ(restored.temperatureUnit, "fahrenheit");
}

TEST(ConfigSerialization, UISettings_TempThresholds_RoundTrip)
{
    UISettings original;
    original.cpuTemperatureGreenBottom = 40;
    original.cpuTemperatureRedTop = 85;
    original.gpuTemperatureGreenBottom = 50;
    original.gpuTemperatureRedTop = 95;

    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<UISettings>(j);

    EXPECT_EQ(restored.cpuTemperatureGreenBottom, 40);
    EXPECT_EQ(restored.cpuTemperatureRedTop, 85);
    EXPECT_EQ(restored.gpuTemperatureGreenBottom, 50);
    EXPECT_EQ(restored.gpuTemperatureRedTop, 95);
}

TEST(ConfigSerialization, UISettings_TempThresholds_MissingKeysUseDefaults)
{
    // Deserialize an empty object into default-constructed UISettings;
    // missing threshold keys should fall back to struct defaults (30, 80, 40, 90).
    nlohmann::json j = nlohmann::json::object();
    auto restored = deserializeRoundtrip<UISettings>(j);

    EXPECT_EQ(restored.cpuTemperatureGreenBottom, 30);
    EXPECT_EQ(restored.cpuTemperatureRedTop, 80);
    EXPECT_EQ(restored.gpuTemperatureGreenBottom, 40);
    EXPECT_EQ(restored.gpuTemperatureRedTop, 90);
}
TEST(ConfigSerialization, UISettings_LegacyKeys_MigrateToCPU)
{
    // JSON with old shared keys should migrate to CPU fields.
    // GPU fields keep their defaults (40, 90).
    nlohmann::json j = {
        {"temperatureGreenBottom", 45},
        {"temperatureRedTop", 85}
    };
    auto restored = deserializeRoundtrip<UISettings>(j);

    EXPECT_EQ(restored.cpuTemperatureGreenBottom, 45);
    EXPECT_EQ(restored.cpuTemperatureRedTop, 85);
    EXPECT_EQ(restored.gpuTemperatureGreenBottom, 40);
    EXPECT_EQ(restored.gpuTemperatureRedTop, 90);
}

TEST(ConfigSerialization, UISettings_SystemResourcesOnly_RoundTrip)
{
    UISettings original;
    original.systemResourcesOnly = true;

    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<UISettings>(j);

    EXPECT_EQ(restored.systemResourcesOnly, true);
}

TEST(ConfigSerialization, UISettings_SystemResourcesOnly_MissingKeyUsesDefault)
{
    // Deserialize an empty object into default-constructed UISettings;
    // missing systemResourcesOnly key should fall back to struct default (false).
    nlohmann::json j = nlohmann::json::object();
    auto restored = deserializeRoundtrip<UISettings>(j);

    EXPECT_EQ(restored.systemResourcesOnly, false);
}


// =============================================================================
// TerminalSettings Tests
// =============================================================================

TEST(ConfigSerialization, TerminalSettings_DefaultRoundtrip) {
    TerminalSettings original;
    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<TerminalSettings>(j);

    EXPECT_EQ(original.defaultShell, restored.defaultShell);
    EXPECT_EQ(original.initialCommand, restored.initialCommand);
    EXPECT_EQ(original.workingDirectory, restored.workingDirectory);
}

TEST(ConfigSerialization, TerminalSettings_ModifiedValues) {
    TerminalSettings original;
    original.defaultShell = "/bin/zsh";
    original.initialCommand = "htop";
    original.workingDirectory = "/home/user/projects";
    original.defaultCols = 120;
    original.defaultRows = 40;

    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<TerminalSettings>(j);

    EXPECT_EQ(restored.defaultShell, "/bin/zsh");
    EXPECT_EQ(restored.initialCommand, "htop");
    EXPECT_EQ(restored.workingDirectory, "/home/user/projects");
    EXPECT_EQ(restored.defaultCols, 120);
    EXPECT_EQ(restored.defaultRows, 40);
}

// =============================================================================
// ModelPreset Tests
// =============================================================================

TEST(ConfigSerialization, ModelPreset_DefaultRoundtrip) {
    ModelPreset original;
    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<ModelPreset>(j);

    // Empty name is normalized to "Unnamed" by validate() during from_json
    EXPECT_EQ(restored.name, "Unnamed");
    EXPECT_EQ(restored.model, original.model);
}

TEST(ConfigSerialization, ModelPreset_ModifiedValues) {
    ModelPreset original;
    original.name = "Creative Writer";
    original.model = "models/mistral-7b.gguf";
    original.inference.temperature = 1.2;
    original.inference.topP = 0.95;

    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<ModelPreset>(j);

    EXPECT_EQ(restored.name, "Creative Writer");
    EXPECT_EQ(restored.model, "models/mistral-7b.gguf");
    EXPECT_FLOAT_EQ(restored.inference.temperature, 1.2);
    EXPECT_FLOAT_EQ(restored.inference.topP, 0.95);
}

// =============================================================================
// TerminalPreset Tests
// =============================================================================

TEST(ConfigSerialization, TerminalPreset_DefaultRoundtrip) {
    TerminalPreset original;
    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<TerminalPreset>(j);

    // Empty name is normalized to "Unnamed" by validate() during from_json
    EXPECT_EQ(restored.name, "Unnamed");
    EXPECT_EQ(restored.initialCommand, original.initialCommand);
}

TEST(ConfigSerialization, TerminalPreset_ModifiedValues) {
    TerminalPreset original;
    original.name = "Monitor";
    original.initialCommand = "htop";
    original.cols = 120;
    original.rows = 40;

    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<TerminalPreset>(j);

    EXPECT_EQ(restored.name, "Monitor");
    EXPECT_EQ(restored.initialCommand, "htop");
    EXPECT_EQ(restored.cols, 120);
    EXPECT_EQ(restored.rows, 40);
}

// =============================================================================
// DiscoverySettings Tests
// =============================================================================

TEST(ConfigSerialization, DiscoverySettings_DefaultRoundtrip) {
    DiscoverySettings original;
    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<DiscoverySettings>(j);

    EXPECT_EQ(original.modelSearchPath, restored.modelSearchPath);
    EXPECT_EQ(original.fileFilter, restored.fileFilter);
}

TEST(ConfigSerialization, DiscoverySettings_ModifiedValues) {
    DiscoverySettings original;
    original.modelSearchPath = "/path/to/models";
    original.fileFilter = {"mmproj*", "*draft*"};

    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<DiscoverySettings>(j);

    EXPECT_EQ(restored.modelSearchPath, "/path/to/models");
    EXPECT_EQ(restored.fileFilter.size(), 2);
}

// =============================================================================
// UserConfig Tests
// =============================================================================

TEST(ConfigSerialization, UserConfig_DefaultRoundtrip) {
    UserConfig original;
    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<UserConfig>(j);

    // Check all top-level fields (load/inference now live per-preset)
    EXPECT_EQ(original.server.host, restored.server.host);
    EXPECT_EQ(original.server.port, restored.server.port);
    EXPECT_EQ(original.ui.theme, restored.ui.theme);
    EXPECT_EQ(original.terminal.defaultShell, restored.terminal.defaultShell);
    EXPECT_EQ(original.discovery.modelSearchPath, restored.discovery.modelSearchPath);
    EXPECT_EQ(original.terminalPresets.size(), restored.terminalPresets.size());
}

TEST(ConfigSerialization, UserConfig_ModifiedValues) {
    UserConfig original;
    original.server.host = "0.0.0.0";
    original.server.port = 8080;
    original.ui.theme = "light";
    original.ui.defaultTab = 1;  // int, not string
    original.terminal.defaultShell = "/usr/bin/bash";
    original.discovery.modelSearchPath = "/models";

    // Model presets (with load/inference) live in models.ini, not UserConfig.

    TerminalPreset termPreset;
    termPreset.name = "Shell";
    termPreset.initialCommand = "bash -i";
    original.terminalPresets.push_back(termPreset);

    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<UserConfig>(j);

    EXPECT_EQ(restored.server.host, "0.0.0.0");
    EXPECT_EQ(restored.server.port, 8080);
    EXPECT_EQ(restored.ui.theme, "light");
    EXPECT_EQ(restored.ui.defaultTab, 1);
    EXPECT_EQ(restored.terminal.defaultShell, "/usr/bin/bash");
    EXPECT_EQ(restored.discovery.modelSearchPath, "/models");
    EXPECT_EQ(restored.terminalPresets.size(), 1);
    EXPECT_EQ(restored.terminalPresets[0].name, "Shell");
}

// =============================================================================
// Advanced InferenceSettings Fields
// =============================================================================

TEST(ConfigSerialization, InferenceSettings_AdvancedFields) {
    InferenceSettings original;
    original.topNsigma = 2.5;
    original.typicalP = 0.8;
    original.xtcProbability = 0.3;
    original.xtcThreshold = 0.2;
    original.dryMultiplier = 1.5;
    original.dryBase = 2.0;
    original.dryAllowedLength = 3;
    original.dryPenaltyLastN = 128;
    original.dynatempRange = 0.5;
    original.dynatempExp = 1.5;
    original.samplers = "topK;topP;temperature";

    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<InferenceSettings>(j);

    EXPECT_FLOAT_EQ(restored.topNsigma, 2.5);
    EXPECT_FLOAT_EQ(restored.typicalP, 0.8);
    EXPECT_FLOAT_EQ(restored.xtcProbability, 0.3);
    EXPECT_FLOAT_EQ(restored.xtcThreshold, 0.2);
    EXPECT_FLOAT_EQ(restored.dryMultiplier, 1.5);
    EXPECT_FLOAT_EQ(restored.dryBase, 2.0);
    EXPECT_EQ(restored.dryAllowedLength, 3);
    EXPECT_EQ(restored.dryPenaltyLastN, 128);
    EXPECT_FLOAT_EQ(restored.dynatempRange, 0.5);
    EXPECT_FLOAT_EQ(restored.dynatempExp, 1.5);
    EXPECT_EQ(restored.samplers, "topK;topP;temperature");
}

TEST(ConfigSerialization, InferenceSettings_Phase4Fields) {
    InferenceSettings original;
    original.ignoreEos = true;
    original.logitBias = "1234,+0.5,-5678,+0.3";
    original.adaptiveTarget = 2.5;
    original.adaptiveDecay = 0.5;
    original.jsonSchemaFile = "/path/schema.json";
    original.grammarFile = "/path/grammar.bnf";
    original.samplerSeq = "tKpT";
    original.drySequenceBreaker = "\n\n";
    original.backendSampling = true;

    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<InferenceSettings>(j);

    EXPECT_EQ(restored.ignoreEos, true);
    EXPECT_EQ(restored.logitBias, "1234,+0.5,-5678,+0.3");
    EXPECT_DOUBLE_EQ(restored.adaptiveTarget, 2.5);
    EXPECT_DOUBLE_EQ(restored.adaptiveDecay, 0.5);
    EXPECT_EQ(restored.jsonSchemaFile, "/path/schema.json");
    EXPECT_EQ(restored.grammarFile, "/path/grammar.bnf");
    EXPECT_EQ(restored.samplerSeq, "tKpT");
    EXPECT_EQ(restored.drySequenceBreaker, "\n\n");
    EXPECT_EQ(restored.backendSampling, true);
}

TEST(ConfigSerialization, LoadSettings_AllFieldsRoundTrip) {
    LoadSettings original;
    original.modelPath = "/path/model.gguf";
    original.modelUrl = "https://example.com/model.gguf";
    original.hfRepo = "org/repo";
    original.hfFile = "model-q4.gguf";
    original.hfToken = "hf_token123";
    original.ngpuLayers = 40;
    original.splitMode = "layer";
    original.tensorSplit = "0.5,0.5";
    original.devicePriority = "0,1";
    original.ctxSize = 16384;
    original.batchSize = 1024;
    original.ubatchSize = 256;
    original.parallel = 2;
    original.cacheTypeK = "q8_0";
    original.cacheTypeV = "q4_0";
    original.kvOffload = false;
    original.kvUnified = false;
    original.flashAttn = "off";
    original.mlock = true;
    original.mmap = true;
    original.threads = 16;
    original.threadsBatch = 8;
    original.lora = "/lora.bin";
    original.mmproj = "/proj.bin";
    original.modelDraft = "/draft.gguf";
    original.draftMax = 32;
    original.specType = "draft-mtp";
    original.cacheTypeKDraft = "q8_0";
    original.cacheTypeVDraft = "q5_1";
    original.deviceDraft = "0,1,2";
    original.preserveThinking = true;
    original.chatTemplate = "chatml";
    original.reasoningFormat = "hidden";
    original.fit = false;

    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<LoadSettings>(j);

    EXPECT_EQ(restored.modelPath, "/path/model.gguf");
    EXPECT_EQ(restored.splitMode, "layer");
    // validate() rounds tensorSplit tokens to nearest hundredth
    EXPECT_EQ(restored.tensorSplit, "0.50,0.50");
    EXPECT_EQ(restored.devicePriority, "0,1");
    EXPECT_EQ(restored.ubatchSize, 256);
    EXPECT_EQ(restored.cacheTypeK, "q8_0");
    EXPECT_EQ(restored.cacheTypeV, "q4_0");
    EXPECT_FALSE(restored.kvOffload);
    EXPECT_FALSE(restored.kvUnified);
    EXPECT_TRUE(restored.mlock);
    EXPECT_TRUE(restored.mmap);
    EXPECT_EQ(restored.threadsBatch, 8);
    EXPECT_EQ(restored.modelDraft, "/draft.gguf");
    EXPECT_EQ(restored.draftMax, 32);
    EXPECT_EQ(restored.specType, "draft-mtp");
    EXPECT_EQ(restored.cacheTypeKDraft, "q8_0");
    EXPECT_EQ(restored.cacheTypeVDraft, "q5_1");
    EXPECT_EQ(restored.deviceDraft, "0,1,2");
    EXPECT_TRUE(restored.preserveThinking);
    EXPECT_EQ(restored.chatTemplate, "chatml");
    EXPECT_EQ(restored.reasoningFormat, "hidden");
    EXPECT_FALSE(restored.fit);
}

TEST(ConfigSerialization, LoadSettings_SpecTypeEmpty_RoundTrip) {
    LoadSettings original;
    original.specType = "";

    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<LoadSettings>(j);

    EXPECT_EQ(restored.specType, "");
}

TEST(ConfigSerialization, LoadSettings_SectionCRoundTrip) {
    LoadSettings original;
    original.nCpuMoe = 8;
    original.cpuMoe = true;
    original.overrideTensor = "ffn.*=CPU";
    original.ropeScaling = "linear";
    original.ropeScale = 1.5;       // 2-decimal-clean for roundToTwoDecimals
    original.ropeFreqBase = 10000.0;
    original.ropeFreqScale = 0.75;
    original.yarnOrigCtx = 2048;
    original.yarnExtFactor = 0.25;
    original.yarnAttnFactor = 1.5;
    original.yarnBetaSlow = 2.0;
    original.yarnBetaFast = 16.0;
    original.swaFull = true;
    original.keep = 128;
    original.numa = "distribute";
    original.fitTarget = "ram";
    original.fitCtx = 4096;
    original.checkTensors = true;
    original.overrideKv = "k=int:1";
    original.loraScaled = "a.gguf=0.5";
    original.controlVector = "/cv.gguf";
    original.controlVectorScaled = "/cv.gguf=0.3";
    original.specDraftNMin = 2;
    original.specDraftPMin = 0.5;
    original.specDraftPSplit = 0.25;
    original.cpuMoeDraft = true;

    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<LoadSettings>(j);

    EXPECT_EQ(restored.nCpuMoe, 8);
    EXPECT_EQ(restored.cpuMoe, true);
    EXPECT_EQ(restored.overrideTensor, "ffn.*=CPU");
    EXPECT_EQ(restored.ropeScaling, "linear");
    EXPECT_DOUBLE_EQ(restored.ropeScale, 1.5);
    EXPECT_DOUBLE_EQ(restored.ropeFreqBase, 10000.0);
    EXPECT_DOUBLE_EQ(restored.ropeFreqScale, 0.75);
    EXPECT_EQ(restored.yarnOrigCtx, 2048);
    EXPECT_DOUBLE_EQ(restored.yarnExtFactor, 0.25);
    EXPECT_DOUBLE_EQ(restored.yarnAttnFactor, 1.5);
    EXPECT_DOUBLE_EQ(restored.yarnBetaSlow, 2.0);
    EXPECT_DOUBLE_EQ(restored.yarnBetaFast, 16.0);
    EXPECT_EQ(restored.swaFull, true);
    EXPECT_EQ(restored.keep, 128);
    EXPECT_EQ(restored.numa, "distribute");
    EXPECT_EQ(restored.fitTarget, "ram");
    EXPECT_EQ(restored.fitCtx, 4096);
    EXPECT_EQ(restored.checkTensors, true);
    EXPECT_EQ(restored.overrideKv, "k=int:1");
    EXPECT_EQ(restored.loraScaled, "a.gguf=0.5");
    EXPECT_EQ(restored.controlVector, "/cv.gguf");
    EXPECT_EQ(restored.controlVectorScaled, "/cv.gguf=0.3");
    EXPECT_EQ(restored.specDraftNMin, 2);
    EXPECT_DOUBLE_EQ(restored.specDraftPMin, 0.5);
    EXPECT_DOUBLE_EQ(restored.specDraftPSplit, 0.25);
    EXPECT_EQ(restored.cpuMoeDraft, true);
}

TEST(ConfigSerialization, RoundToTwoDecimals_Precision) {
    InferenceSettings original;
    original.temperature = 0.12345;
    original.topP = 0.99999;
    original.minP = 0.001;

    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<InferenceSettings>(j);

    // roundToTwoDecimals should round to 2 decimal places
    EXPECT_FLOAT_EQ(restored.temperature, 0.12);
    EXPECT_FLOAT_EQ(restored.topP, 1.0);
    EXPECT_FLOAT_EQ(restored.minP, 0.0);
}

TEST(ConfigSerialization, FromJson_EmptyJson_UsesDefaults) {
    nlohmann::json j = "{}"_json;
    UserConfig config;
    from_json(j, config);

    UserConfig defaults;
    EXPECT_EQ(config.server.host, defaults.server.host);
    EXPECT_EQ(config.server.port, defaults.server.port);
    EXPECT_EQ(config.ui.theme, defaults.ui.theme);
    EXPECT_EQ(config.terminalPresets.size(), defaults.terminalPresets.size());
}

TEST(ConfigSerialization, ModelPreset_NestedRoundTrip) {
    ModelPreset original;
    original.name = "full-preset";
    original.model = "/m.gguf";
    original.load.ctxSize = 8192;
    original.load.ngpuLayers = -1;
    original.load.flashAttn = "on";
    original.load.fit = true;
    original.inference.temperature = 0.7;
    original.inference.topP = 0.9;
    original.inference.topK = 50;
    original.inference.nPredict = 1024;

    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<ModelPreset>(j);

    EXPECT_EQ(restored.name, "full-preset");
    EXPECT_EQ(restored.model, "/m.gguf");
    EXPECT_EQ(restored.load.ctxSize, 8192);
    EXPECT_EQ(restored.load.ngpuLayers, -1);
    EXPECT_TRUE(restored.load.fit);
    EXPECT_FLOAT_EQ(restored.inference.temperature, 0.7);
    EXPECT_EQ(restored.inference.topK, 50);
    EXPECT_EQ(restored.inference.nPredict, 1024);
}

// =============================================================================
// Missing Keys Test
// =============================================================================

TEST(ConfigSerialization, MissingKeys_UseDefaults) {
    // JSON with only partial fields should use defaults for missing ones
    nlohmann::json j = R"({
        "server": {
            "host": "192.168.1.1"
        }
    })"_json;

    UserConfig config;
    from_json(j, config);

    // server.host should be from JSON
    EXPECT_EQ(config.server.host, "192.168.1.1");
    // server.port should default
    EXPECT_EQ(config.server.port, 8080);
}

TEST(ConfigSerialization, LegacyLoadInferenceKeys_Ignored) {
    // Older config.json files carried top-level "load"/"inference" blocks.
    // They are no longer part of UserConfig and must be ignored without error.
    nlohmann::json j = R"({
        "server": { "host": "10.0.0.1" },
        "load": { "modelPath": "old.gguf", "ngpuLayers": 5 },
        "inference": { "temperature": 1.9 }
    })"_json;

    UserConfig config;
    EXPECT_NO_THROW(from_json(j, config));
    EXPECT_EQ(config.server.host, "10.0.0.1");
}

// =============================================================================
// VllmSettings Tests
// =============================================================================

TEST(ConfigSerialization, VllmSettings_DefaultRoundtrip) {
    VllmSettings original;
    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<VllmSettings>(j);

    EXPECT_EQ(original.host, restored.host);
    EXPECT_EQ(original.port, restored.port);
}

TEST(ConfigSerialization, VllmSettings_ModifiedValues) {
    VllmSettings original;
    original.host = "192.168.1.100";
    original.port = 8100;

    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<VllmSettings>(j);

    EXPECT_EQ(restored.host, "192.168.1.100");
    EXPECT_EQ(restored.port, 8100);
}

TEST(ConfigSerialization, VllmSettings_MissingKeysUseDefaults) {
    // An empty object should deserialize to default-constructed values.
    nlohmann::json j = nlohmann::json::object();
    auto restored = deserializeRoundtrip<VllmSettings>(j);
    VllmSettings defaults;

    EXPECT_EQ(restored.host, defaults.host);
    EXPECT_EQ(restored.port, defaults.port);
}

TEST(ConfigSerialization, VllmSettings_PortClamped) {
    VllmSettings original;
    original.host = "localhost";
    original.port = 99999; // Out of range

    auto j = serializeRoundtrip(original);
    auto restored = deserializeRoundtrip<VllmSettings>(j);

    // validate() clamps port to [1, 65535]
    EXPECT_EQ(restored.port, 65535);
}

TEST(ConfigSerialization, UserConfig_IncludesVllmSettings) {
    UserConfig original;
    original.vllm.host = "10.0.0.50";
    original.vllm.port = 8200;

    auto j = serializeRoundtrip(original);
    ASSERT_TRUE(j.contains("vllm"));
    auto restored = deserializeRoundtrip<UserConfig>(j);

    EXPECT_EQ(restored.vllm.host, "10.0.0.50");
    EXPECT_EQ(restored.vllm.port, 8200);
}

TEST(ConfigSerialization, UserConfig_VllmMissingKeyUsesDefaults) {
    // JSON with no "vllm" key should use defaults.
    nlohmann::json j = R"({
        "server": { "host": "127.0.0.1" }
    })"_json;

    UserConfig config;
    from_json(j, config);

    EXPECT_EQ(config.vllm.host, "");
    EXPECT_EQ(config.vllm.port, 8000);
}