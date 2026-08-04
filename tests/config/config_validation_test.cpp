/**
 * @file config_validation_test.cpp
 * @brief Tests for validate() / validateAll() on all config structs.
 *
 * Covers:
 * - Per-field clamping for LoadSettings, InferenceSettings, ServerSettings,
 *   UISettings, TerminalSettings, TerminalPreset, ModelPreset
 * - Cross-struct rules in UserConfig::validateAll()
 * - Duplicate preset name removal
 * - ngpuLayers backward-compat deserialization
 * - TerminalSettings name round-trip (serialization bug fix)
 * - No-op correctness for DiscoverySettings
 * - Default-constructed UserConfig survives validateAll() unchanged
 */

#include <gtest/gtest.h>
#include "config/config.h"
#include "json.hpp"

using namespace Config;
using json = nlohmann::json;

// =============================================================================
// Shared fixture
// =============================================================================

class ConfigValidationTest : public ::testing::Test
{
protected:
    UserConfig cfg;
    // Load/inference settings now live per-preset (models.ini), not as
    // top-level UserConfig members. These standalone structs exercise the
    // LoadSettings/InferenceSettings validate() logic directly.
    LoadSettings load;
    InferenceSettings inference;
};

// =============================================================================
// 1. LoadSettings per-field clamps
// =============================================================================

TEST_F(ConfigValidationTest, LoadSettings_NgpuLayers_ClampsBelowMin)
{
    load.ngpuLayers = -2;
    load.validate();
    EXPECT_EQ(load.ngpuLayers, -1);
}

TEST_F(ConfigValidationTest, LoadSettings_NgpuLayers_ClampsAboveMax)
{
    load.ngpuLayers = 200;
    load.validate();
    EXPECT_EQ(load.ngpuLayers, 99);
}

TEST_F(ConfigValidationTest, LoadSettings_NgpuLayers_ValidValuesUnchanged)
{
    for (int v : {-1, 0, 33, 99}) {
        load.ngpuLayers = v;
        load.validate();
        EXPECT_EQ(load.ngpuLayers, v);
    }
}

TEST_F(ConfigValidationTest, LoadSettings_CtxSize_ZeroUnchanged)
{
    // 0 = "use model default" — must not be rounded or clamped
    load.ctxSize = 0;
    load.validate();
    EXPECT_EQ(load.ctxSize, 0);
}

TEST_F(ConfigValidationTest, LoadSettings_CtxSize_NegativeBecomesDefault)
{
    // A negative context is nonsensical → treated as 0 (model default).
    load.ctxSize = -5;
    load.validate();
    EXPECT_EQ(load.ctxSize, 0);
}

TEST_F(ConfigValidationTest, LoadSettings_CtxSize_ArbitraryValuePassesThrough)
{
    // The model's true maximum is unknown here, so any positive value the user
    // sets is passed through verbatim — NOT rounded to a power of 2.
    load.ctxSize = 5000;
    load.validate();
    EXPECT_EQ(load.ctxSize, 5000);

    load.ctxSize = 8193;
    load.validate();
    EXPECT_EQ(load.ctxSize, 8193);
}

TEST_F(ConfigValidationTest, LoadSettings_CtxSize_NoUpperClamp)
{
    // No enforced upper bound — a very large context survives validation.
    load.ctxSize = 200000;
    load.validate();
    EXPECT_EQ(load.ctxSize, 200000);
}

TEST_F(ConfigValidationTest, LoadSettings_CtxSize_SmallValueNotClampedUp)
{
    // Previously 100 clamped up to 256; now small positive values are kept.
    load.ctxSize = 100;
    load.validate();
    EXPECT_EQ(load.ctxSize, 100);
}

TEST_F(ConfigValidationTest, LoadSettings_BatchSize_ClampsBelowMin)
{
    load.batchSize = 0;
    load.validate();
    EXPECT_EQ(load.batchSize, 1);
}

TEST_F(ConfigValidationTest, LoadSettings_BatchSize_ClampsAboveMax)
{
    load.batchSize = 99999;
    load.validate();
    EXPECT_EQ(load.batchSize, 65536);
}

TEST_F(ConfigValidationTest, LoadSettings_UbatchSize_ClampsBelowMin)
{
    load.ubatchSize = 0;
    load.validate();
    EXPECT_EQ(load.ubatchSize, 1);
}

TEST_F(ConfigValidationTest, LoadSettings_UbatchSize_ClampsAboveMax)
{
    load.ubatchSize = 99999;
    load.validate();
    EXPECT_EQ(load.ubatchSize, 4096);
}


TEST_F(ConfigValidationTest, LoadSettings_CacheTypeK_InvalidDefaultsToF16)
{
    load.cacheTypeK = "invalid";
    load.validate();
    EXPECT_EQ(load.cacheTypeK, "f16");
}

TEST_F(ConfigValidationTest, LoadSettings_CacheTypeK_AllNineValuesUnchanged)
{
    for (const auto &t : {"f32", "f16", "bf16", "q8_0", "q4_0", "q4_1",
                          "iq4_nl", "q5_0", "q5_1"}) {
        load.cacheTypeK = t;
        load.validate();
        EXPECT_EQ(load.cacheTypeK, t);
    }
}

// --- Section C enum + clamp validation ---

TEST_F(ConfigValidationTest, LoadSettings_RopeScaling_InvalidResetsToEmpty)
{
    load.ropeScaling = "bogus";
    load.validate();
    EXPECT_EQ(load.ropeScaling, "");
}

TEST_F(ConfigValidationTest, LoadSettings_RopeScaling_ValidUnchanged)
{
    for (const auto &t : {"none", "linear", "yarn"}) {
        load.ropeScaling = t;
        load.validate();
        EXPECT_EQ(load.ropeScaling, t);
    }
}

TEST_F(ConfigValidationTest, LoadSettings_Numa_InvalidResetsToEmpty)
{
    load.numa = "wrong";
    load.validate();
    EXPECT_EQ(load.numa, "");
}

TEST_F(ConfigValidationTest, LoadSettings_FitTarget_InvalidResetsToEmpty)
{
    load.fitTarget = "gpu";
    load.validate();
    EXPECT_EQ(load.fitTarget, "");
}

TEST_F(ConfigValidationTest, LoadSettings_NCpuMoe_NegativeOnePreserved)
{
    load.nCpuMoe = -1;
    load.validate();
    EXPECT_EQ(load.nCpuMoe, -1);
}

TEST_F(ConfigValidationTest, LoadSettings_Keep_NegativeOnePreserved)
{
    load.keep = -1;
    load.validate();
    EXPECT_EQ(load.keep, -1);
}

TEST_F(ConfigValidationTest, LoadSettings_CacheTypeV_InvalidDefaultsToF16)
{
    load.cacheTypeV = "fp32";
    load.validate();
    EXPECT_EQ(load.cacheTypeV, "f16");
}

TEST_F(ConfigValidationTest, LoadSettings_DraftCacheTypeK_AllNineValuesUnchanged)
{
    for (const auto &t : {"f32", "f16", "bf16", "q8_0", "q4_0", "q4_1",
                          "iq4_nl", "q5_0", "q5_1"}) {
        load.cacheTypeKDraft = t;
        load.validate();
        EXPECT_EQ(load.cacheTypeKDraft, t);
    }
}

TEST_F(ConfigValidationTest, LoadSettings_DraftCacheTypeV_AllNineValuesUnchanged)
{
    for (const auto &t : {"f32", "f16", "bf16", "q8_0", "q4_0", "q4_1",
                          "iq4_nl", "q5_0", "q5_1"}) {
        load.cacheTypeVDraft = t;
        load.validate();
        EXPECT_EQ(load.cacheTypeVDraft, t);
    }
}

TEST_F(ConfigValidationTest, LoadSettings_DraftCacheTypeK_InvalidResetsToF16)
{
    load.cacheTypeKDraft = "invalid";
    load.validate();
    EXPECT_EQ(load.cacheTypeKDraft, "f16");
}

TEST_F(ConfigValidationTest, LoadSettings_DraftCacheTypeV_InvalidResetsToF16)
{
    load.cacheTypeVDraft = "bogus";
    load.validate();
    EXPECT_EQ(load.cacheTypeVDraft, "f16");
}

TEST_F(ConfigValidationTest, LoadSettings_DeviceDraft_FreeFormUnchanged)
{
    load.deviceDraft = "0,1,2";
    load.validate();
    EXPECT_EQ(load.deviceDraft, "0,1,2");
}

TEST_F(ConfigValidationTest, LoadSettings_PreserveThinking_BoolUnchanged)
{
    load.preserveThinking = true;
    load.validate();
    EXPECT_TRUE(load.preserveThinking);
    load.preserveThinking = false;
    load.validate();
    EXPECT_FALSE(load.preserveThinking);
}

TEST_F(ConfigValidationTest, LoadSettings_Threads_ClampsBelowMin)
{
    load.threads = -5;
    load.validate();
    EXPECT_EQ(load.threads, -1);
}

TEST_F(ConfigValidationTest, LoadSettings_Threads_ClampsAboveMax)
{
    load.threads = 2000;
    load.validate();
    EXPECT_EQ(load.threads, 1024);
}

// =============================================================================
// 2. InferenceSettings per-field clamps
// =============================================================================

TEST_F(ConfigValidationTest, InferenceSettings_Temperature_ClampsBelowMin)
{
    inference.temperature = -1.0;
    inference.validate();
    EXPECT_DOUBLE_EQ(inference.temperature, 0.0);
}

TEST_F(ConfigValidationTest, InferenceSettings_Temperature_ClampsAboveMax)
{
    inference.temperature = 5.0;
    inference.validate();
    EXPECT_DOUBLE_EQ(inference.temperature, 2.0);
}

TEST_F(ConfigValidationTest, InferenceSettings_Mirostat_ClampsBelowMin)
{
    inference.mirostat = -1;
    inference.validate();
    EXPECT_EQ(inference.mirostat, 0);
}

TEST_F(ConfigValidationTest, InferenceSettings_Mirostat_ClampsAboveMax)
{
    inference.mirostat = 5;
    inference.validate();
    EXPECT_EQ(inference.mirostat, 2);
}

TEST_F(ConfigValidationTest, InferenceSettings_RepeatPenalty_ClampsBelowMin)
{
    inference.repeatPenalty = 0.1;
    inference.validate();
    EXPECT_DOUBLE_EQ(inference.repeatPenalty, 0.5);
}

TEST_F(ConfigValidationTest, InferenceSettings_RepeatPenalty_ClampsAboveMax)
{
    inference.repeatPenalty = 10.0;
    inference.validate();
    EXPECT_DOUBLE_EQ(inference.repeatPenalty, 3.0);
}

// =============================================================================
// 2b. Phase 4 InferenceSettings per-field clamps
// =============================================================================

TEST_F(ConfigValidationTest, InferenceSettings_AdaptiveTarget_ClampsBelowMin)
{
    inference.adaptiveTarget = -5.0;
    inference.validate();
    EXPECT_DOUBLE_EQ(inference.adaptiveTarget, 0.0);
}

TEST_F(ConfigValidationTest, InferenceSettings_AdaptiveTarget_ClampsAboveMax)
{
    inference.adaptiveTarget = 9999.0;
    inference.validate();
    EXPECT_DOUBLE_EQ(inference.adaptiveTarget, 1024.0);
}

TEST_F(ConfigValidationTest, InferenceSettings_AdaptiveDecay_ClampsBelowMin)
{
    inference.adaptiveDecay = -1.0;
    inference.validate();
    EXPECT_DOUBLE_EQ(inference.adaptiveDecay, 0.0);
}

TEST_F(ConfigValidationTest, InferenceSettings_AdaptiveDecay_ClampsAboveMax)
{
    inference.adaptiveDecay = 2.0;
    inference.validate();
    EXPECT_DOUBLE_EQ(inference.adaptiveDecay, 1.0);
}

// =============================================================================
// 3. ApiSettings per-field clamps
// =============================================================================

TEST_F(ConfigValidationTest, ApiSettings_Port_ClampsBelowMin)
{
    cfg.api.apiPort = 0;
    cfg.api.validate();
    EXPECT_EQ(cfg.api.apiPort, 1);
}

TEST_F(ConfigValidationTest, ApiSettings_Port_ClampsAboveMax)
{
    cfg.api.apiPort = 99999;
    cfg.api.validate();
    EXPECT_EQ(cfg.api.apiPort, 65535);
}

TEST_F(ConfigValidationTest, ApiSettings_Port_ValidValueUnchanged)
{
    cfg.api.apiPort = 9090;
    cfg.api.validate();
    EXPECT_EQ(cfg.api.apiPort, 9090);
}

TEST_F(ConfigValidationTest, ApiSettings_ValidateAll_ClampsApiPort)
{
    cfg.api.apiPort = -1;
    cfg.validateAll();
    EXPECT_EQ(cfg.api.apiPort, 1);
}

// =============================================================================
// 3. ServerSettings per-field clamps + cross-field
// =============================================================================

TEST_F(ConfigValidationTest, ServerSettings_Port_ClampsBelowMin)
{
    cfg.server.port = 0;
    cfg.server.validate();
    EXPECT_EQ(cfg.server.port, 1);
}

TEST_F(ConfigValidationTest, ServerSettings_Port_ClampsAboveMax)
{
    cfg.server.port = 99999;
    cfg.server.validate();
    EXPECT_EQ(cfg.server.port, 65535);
}

TEST_F(ConfigValidationTest, ServerSettings_CacheRam_ClampsBelowMin)
{
    cfg.server.cacheRam = -5;
    cfg.server.validate();
    EXPECT_EQ(cfg.server.cacheRam, 0);
}

TEST_F(ConfigValidationTest, ServerSettings_CacheRam_ClampsAboveMax)
{
    cfg.server.cacheRam = 9999999;
    cfg.server.validate();
    EXPECT_EQ(cfg.server.cacheRam, 1048576);
}

TEST_F(ConfigValidationTest, ServerSettings_CacheRam_ValidValueUnchanged)
{
    cfg.server.cacheRam = 4096;
    cfg.server.validate();
    EXPECT_EQ(cfg.server.cacheRam, 4096);
}

TEST_F(ConfigValidationTest, ServerSettings_CtxCheckpoints_ClampsBelowMin)
{
    cfg.server.ctxCheckpoints = -5;
    cfg.server.validate();
    EXPECT_EQ(cfg.server.ctxCheckpoints, 0);
}

TEST_F(ConfigValidationTest, ServerSettings_CtxCheckpoints_ClampsAboveMax)
{
    cfg.server.ctxCheckpoints = 9999;
    cfg.server.validate();
    EXPECT_EQ(cfg.server.ctxCheckpoints, 256);
}

TEST_F(ConfigValidationTest, ServerSettings_CtxCheckpoints_ValidValueUnchanged)
{
    cfg.server.ctxCheckpoints = 16;
    cfg.server.validate();
    EXPECT_EQ(cfg.server.ctxCheckpoints, 16);
}

TEST_F(ConfigValidationTest, ServerSettings_SSL_DerivesKeyFileWhenMissing)
{
    cfg.server.sslCertFile = "server.crt";
    cfg.server.sslKeyFile  = "";
    cfg.server.validate();
    EXPECT_EQ(cfg.server.sslKeyFile, "server.crt.key");
}

TEST_F(ConfigValidationTest, ServerSettings_SSL_DoesNotOverrideExplicitKeyFile)
{
    cfg.server.sslCertFile = "server.crt";
    cfg.server.sslKeyFile  = "explicit.key";
    cfg.server.validate();
    EXPECT_EQ(cfg.server.sslKeyFile, "explicit.key");
}

TEST_F(ConfigValidationTest, ServerSettings_SSL_NoCertNoChange)
{
    cfg.server.sslCertFile = "";
    cfg.server.sslKeyFile  = "";
    cfg.server.validate();
    EXPECT_EQ(cfg.server.sslKeyFile, "");
}

// --- Section A router params ---

TEST_F(ConfigValidationTest, ServerSettings_ModelsMax_ClampsBelowMin)
{
    cfg.server.modelsMax = 0;
    cfg.server.validate();
    EXPECT_EQ(cfg.server.modelsMax, 1);
}

TEST_F(ConfigValidationTest, ServerSettings_ModelsMax_ClampsAboveMax)
{
    cfg.server.modelsMax = 999;
    cfg.server.validate();
    EXPECT_EQ(cfg.server.modelsMax, 64);
}

TEST_F(ConfigValidationTest, ServerSettings_Pooling_InvalidResetsToEmpty)
{
    cfg.server.pooling = "bogus";
    cfg.server.validate();
    EXPECT_EQ(cfg.server.pooling, "");
}

TEST_F(ConfigValidationTest, ServerSettings_Pooling_ValidUnchanged)
{
    cfg.server.pooling = "rank";
    cfg.server.validate();
    EXPECT_EQ(cfg.server.pooling, "rank");
}

TEST_F(ConfigValidationTest, ServerSettings_Reasoning_InvalidResetsToAuto)
{
    cfg.server.reasoning = "nonsense";
    cfg.server.validate();
    EXPECT_EQ(cfg.server.reasoning, "auto");
}

TEST_F(ConfigValidationTest, ServerSettings_Reasoning_ValidUnchanged)
{
    cfg.server.reasoning = "off";
    cfg.server.validate();
    EXPECT_EQ(cfg.server.reasoning, "off");
}

TEST_F(ConfigValidationTest, ServerSettings_EmbdNormalize_NegativeOnePreserved)
{
    cfg.server.embdNormalize = -1;
    cfg.server.validate();
    EXPECT_EQ(cfg.server.embdNormalize, -1);
}

TEST_F(ConfigValidationTest, ServerSettings_ReasoningBudget_NegativeOnePreserved)
{
    cfg.server.reasoningBudget = -1;
    cfg.server.validate();
    EXPECT_EQ(cfg.server.reasoningBudget, -1);
}

// =============================================================================
// 4. UISettings per-field clamps
// =============================================================================

TEST_F(ConfigValidationTest, UISettings_DefaultTab_ClampsBelowMin)
{
    cfg.ui.defaultTab = -1;
    cfg.ui.validate();
    EXPECT_EQ(cfg.ui.defaultTab, 0);
}

TEST_F(ConfigValidationTest, UISettings_DefaultTab_ClampsAboveMax)
{
    cfg.ui.defaultTab = 100;
    cfg.ui.validate();
    EXPECT_EQ(cfg.ui.defaultTab, 10);
}

TEST_F(ConfigValidationTest, UISettings_RefreshRateMs_ClampsBelowMin)
{
    cfg.ui.refreshRateMs = 5;
    cfg.ui.validate();
    EXPECT_EQ(cfg.ui.refreshRateMs, 20);
}

TEST_F(ConfigValidationTest, UISettings_TemperatureUnit_InvalidResetsToCelsius)
{
    cfg.ui.temperatureUnit = "kelvin";
    cfg.ui.validate();
    EXPECT_EQ(cfg.ui.temperatureUnit, "celsius");
}

TEST_F(ConfigValidationTest, UISettings_TemperatureUnit_ValidUnchanged)
{
    cfg.ui.temperatureUnit = "fahrenheit";
    cfg.ui.validate();
    EXPECT_EQ(cfg.ui.temperatureUnit, "fahrenheit");
}

// --- Temperature threshold validation ---

TEST_F(ConfigValidationTest, UISettings_CpuTempGreenBottom_ClampsBelowMin)
{
    cfg.ui.cpuTemperatureGreenBottom = -100;
    cfg.ui.validate();
    EXPECT_EQ(cfg.ui.cpuTemperatureGreenBottom, -50);
}

TEST_F(ConfigValidationTest, UISettings_CpuTempGreenBottom_ClampsAboveMax)
{
    cfg.ui.cpuTemperatureGreenBottom = 300;
    cfg.ui.validate();
    EXPECT_EQ(cfg.ui.cpuTemperatureGreenBottom, 200);
}

TEST_F(ConfigValidationTest, UISettings_CpuTempRedTop_ClampsBelowMin)
{
    cfg.ui.cpuTemperatureRedTop = -100;
    cfg.ui.validate();
    EXPECT_EQ(cfg.ui.cpuTemperatureRedTop, -50);
}

TEST_F(ConfigValidationTest, UISettings_CpuTempRedTop_ClampsAboveMax)
{
    cfg.ui.cpuTemperatureRedTop = 300;
    cfg.ui.validate();
    EXPECT_EQ(cfg.ui.cpuTemperatureRedTop, 200);
}

TEST_F(ConfigValidationTest, UISettings_CpuTempThresholds_SwappedWhenGreenExceedsRed)
{
    cfg.ui.cpuTemperatureGreenBottom = 90;
    cfg.ui.cpuTemperatureRedTop = 40;
    cfg.ui.validate();
    EXPECT_EQ(cfg.ui.cpuTemperatureGreenBottom, 40);
    EXPECT_EQ(cfg.ui.cpuTemperatureRedTop, 90);
}

TEST_F(ConfigValidationTest, UISettings_CpuTempThresholds_EqualValuesPreserved)
{
    cfg.ui.cpuTemperatureGreenBottom = 50;
    cfg.ui.cpuTemperatureRedTop = 50;
    cfg.ui.validate();
    EXPECT_EQ(cfg.ui.cpuTemperatureGreenBottom, 50);
    EXPECT_EQ(cfg.ui.cpuTemperatureRedTop, 50);
}
TEST_F(ConfigValidationTest, UISettings_GpuTempGreenBottom_ClampsBelowMin)
{
    cfg.ui.gpuTemperatureGreenBottom = -100;
    cfg.ui.validate();
    EXPECT_EQ(cfg.ui.gpuTemperatureGreenBottom, -50);
}

TEST_F(ConfigValidationTest, UISettings_GpuTempGreenBottom_ClampsAboveMax)
{
    cfg.ui.gpuTemperatureGreenBottom = 300;
    cfg.ui.validate();
    EXPECT_EQ(cfg.ui.gpuTemperatureGreenBottom, 200);
}

TEST_F(ConfigValidationTest, UISettings_GpuTempRedTop_ClampsBelowMin)
{
    cfg.ui.gpuTemperatureRedTop = -100;
    cfg.ui.validate();
    EXPECT_EQ(cfg.ui.gpuTemperatureRedTop, -50);
}

TEST_F(ConfigValidationTest, UISettings_GpuTempRedTop_ClampsAboveMax)
{
    cfg.ui.gpuTemperatureRedTop = 300;
    cfg.ui.validate();
    EXPECT_EQ(cfg.ui.gpuTemperatureRedTop, 200);
}

TEST_F(ConfigValidationTest, UISettings_GpuTempThresholds_SwappedWhenGreenExceedsRed)
{
    cfg.ui.gpuTemperatureGreenBottom = 90;
    cfg.ui.gpuTemperatureRedTop = 40;
    cfg.ui.validate();
    EXPECT_EQ(cfg.ui.gpuTemperatureGreenBottom, 40);
    EXPECT_EQ(cfg.ui.gpuTemperatureRedTop, 90);
}

TEST_F(ConfigValidationTest, UISettings_GpuTempThresholds_EqualValuesPreserved)
{
    cfg.ui.gpuTemperatureGreenBottom = 50;
    cfg.ui.gpuTemperatureRedTop = 50;
    cfg.ui.validate();
    EXPECT_EQ(cfg.ui.gpuTemperatureGreenBottom, 50);
    EXPECT_EQ(cfg.ui.gpuTemperatureRedTop, 50);
}

TEST_F(ConfigValidationTest, UISettings_CpuAndGpuValidation_Independent)
{
    // CPU green clamps to 200, GPU green clamps to -50 — independent.
    cfg.ui.cpuTemperatureGreenBottom = 250;
    cfg.ui.gpuTemperatureGreenBottom = -100;
    cfg.ui.validate();
    EXPECT_EQ(cfg.ui.cpuTemperatureGreenBottom, 200);
    EXPECT_EQ(cfg.ui.gpuTemperatureGreenBottom, -50);
}


// =============================================================================
// 5. Name defaults
// =============================================================================

TEST_F(ConfigValidationTest, ModelPreset_EmptyName_BecomesUnnamed)
{
    ModelPreset p;
    p.name = "";
    p.validate();
    EXPECT_EQ(p.name, "Unnamed");
}

TEST_F(ConfigValidationTest, ModelPreset_NonEmptyName_Unchanged)
{
    ModelPreset p;
    p.name = "My Preset";
    p.validate();
    EXPECT_EQ(p.name, "My Preset");
}

TEST_F(ConfigValidationTest, TerminalPreset_EmptyName_BecomesUnnamed)
{
    TerminalPreset tp;
    tp.name = "";
    tp.validate();
    EXPECT_EQ(tp.name, "Unnamed");
}

TEST_F(ConfigValidationTest, TerminalPreset_NonEmptyName_Unchanged)
{
    TerminalPreset tp;
    tp.name = "Dev";
    tp.validate();
    EXPECT_EQ(tp.name, "Dev");
}

TEST_F(ConfigValidationTest, TerminalSettings_EmptyName_BecomesUnnamed)
{
    cfg.terminal.name = "";
    cfg.terminal.validate();
    EXPECT_EQ(cfg.terminal.name, "Unnamed");
}

TEST_F(ConfigValidationTest, TerminalSettings_NonEmptyName_Unchanged)
{
    cfg.terminal.name = "Root Shell";
    cfg.terminal.validate();
    EXPECT_EQ(cfg.terminal.name, "Root Shell");
}

// =============================================================================
// 6. ModelPreset load/inference field validation (presets live in models.ini)
//
// Model presets are no longer part of UserConfig. The per-field clamps are
// still exercised directly on the preset's LoadSettings/InferenceSettings.
// =============================================================================

TEST_F(ConfigValidationTest, ModelPreset_LoadInference_FieldsValidate)
{
    ModelPreset p;
    p.load.ctxSize          = 3000;   // passed through verbatim (no rounding)
    p.inference.temperature = 5.0;    // clamps to 2.0
    p.load.validate();
    p.inference.validate();
    EXPECT_EQ(p.load.ctxSize, 3000);
    EXPECT_DOUBLE_EQ(p.inference.temperature, 2.0);
}

// =============================================================================
// 8. ngpuLayers JSON deserialization (int only)
// =============================================================================

TEST_F(ConfigValidationTest, NgpuLayers_JsonNumber_AutoSentinel)
{
    json j = R"({"ngpuLayers": -1})"_json;
    LoadSettings s;
    from_json(j, s);
    EXPECT_EQ(s.ngpuLayers, -1);
}

TEST_F(ConfigValidationTest, NgpuLayers_JsonNumber_ExplicitCount)
{
    json j = R"({"ngpuLayers": 42})"_json;
    LoadSettings s;
    from_json(j, s);
    EXPECT_EQ(s.ngpuLayers, 42);
}

TEST_F(ConfigValidationTest, NgpuLayers_JsonNumber_Zero)
{
    json j = R"({"ngpuLayers": 0})"_json;
    LoadSettings s;
    from_json(j, s);
    EXPECT_EQ(s.ngpuLayers, 0);
}

TEST_F(ConfigValidationTest, NgpuLayers_MissingKeyKeepsDefault)
{
    json j = "{}"_json;
    LoadSettings s;  // default = -1
    from_json(j, s);
    EXPECT_EQ(s.ngpuLayers, -1);
}

// =============================================================================
// 9. No-op on valid values (default-constructed UserConfig)
// =============================================================================

TEST_F(ConfigValidationTest, ValidateAll_DefaultConfig_NoChanges)
{
    UserConfig original;
    UserConfig validated;
    validated.validateAll();

    EXPECT_EQ(validated.server.port,           original.server.port);
    EXPECT_EQ(validated.ui.defaultTab,         original.ui.defaultTab);
    EXPECT_EQ(validated.ui.refreshRateMs,      original.ui.refreshRateMs);
}

// =============================================================================
// 10. LoadSettings — tensorSplit sanitization
// =============================================================================

TEST_F(ConfigValidationTest, LoadSettings_TensorSplit_ValidDecimals_Preserved)
{
    load.tensorSplit = "1.50,60.30";
    load.validate();
    EXPECT_EQ(load.tensorSplit, "1.50,60.30");
}

TEST_F(ConfigValidationTest, LoadSettings_TensorSplit_IntegerValues_RoundedToHundredth)
{
    load.tensorSplit = "1,5";
    load.validate();
    EXPECT_EQ(load.tensorSplit, "1.00,5.00");
}

TEST_F(ConfigValidationTest, LoadSettings_TensorSplit_ManyDecimalPlaces_Rounded)
{
    load.tensorSplit = "1.555,2.444";
    load.validate();
    EXPECT_EQ(load.tensorSplit, "1.56,2.44");
}

TEST_F(ConfigValidationTest, LoadSettings_TensorSplit_GarbageToken_ReplacedWithDefault)
{
    load.tensorSplit = "abc,2.0";
    load.validate();
    EXPECT_EQ(load.tensorSplit, "1.00,2.00");
}

TEST_F(ConfigValidationTest, LoadSettings_TensorSplit_NegativeValue_ReplacedWithDefault)
{
    load.tensorSplit = "-1.0,2.0";
    load.validate();
    EXPECT_EQ(load.tensorSplit, "1.00,2.00");
}

TEST_F(ConfigValidationTest, LoadSettings_TensorSplit_Empty_RemainsEmpty)
{
    load.tensorSplit = "";
    load.validate();
    EXPECT_EQ(load.tensorSplit, "");
}

// =============================================================================
// 12. DiscoverySettings no-op validate
// =============================================================================

TEST_F(ConfigValidationTest, DiscoverySettings_ValidateIsNoOp)
{
    cfg.discovery.modelSearchPath = "/some/path";
    const auto pathBefore = cfg.discovery.modelSearchPath;
    cfg.discovery.validate();
    EXPECT_EQ(cfg.discovery.modelSearchPath, pathBefore);
}

// =============================================================================
// 13. TerminalSettings name round-trip (serialization bug fix)
// =============================================================================

TEST_F(ConfigValidationTest, TerminalSettings_NameRoundTrip)
{
    TerminalSettings ts;
    ts.name         = "MyTerminal";
    ts.defaultShell = "/bin/bash";
    ts.defaultCols  = 120;
    ts.defaultRows  = 40;

    json j;
    to_json(j, ts);

    // "name" must be present in the serialized output
    ASSERT_TRUE(j.contains("name"));
    EXPECT_EQ(j["name"].get<std::string>(), "MyTerminal");

    TerminalSettings restored;
    from_json(j, restored);
    EXPECT_EQ(restored.name, "MyTerminal");
    EXPECT_EQ(restored.defaultShell, "/bin/bash");
    EXPECT_EQ(restored.defaultCols, 120);
    EXPECT_EQ(restored.defaultRows, 40);
}

// =============================================================================
// 14. LoadSettings::specType validation
// =============================================================================

TEST_F(ConfigValidationTest, LoadSettings_SpecType_EmptyPreserved)
{
    load.specType = "";
    load.validate();
    EXPECT_EQ(load.specType, "");
}

TEST_F(ConfigValidationTest, LoadSettings_SpecType_ValidValuesPreserved)
{
    for (const auto *t : {"none", "draft-simple", "draft-eagle3", "draft-mtp",
                          "draft-dflash", "ngram-simple", "ngram-map-k", "ngram-map-k4v",
                          "ngram-mod", "ngram-cache"}) {
        load.specType = t;
        load.validate();
        EXPECT_EQ(load.specType, t);
    }
}

TEST_F(ConfigValidationTest, LoadSettings_SpecType_InvalidClearsField)
{
    load.specType = "bogus-type";
    load.validate();
    EXPECT_EQ(load.specType, "");
}

TEST_F(ConfigValidationTest, LoadSettings_SpecType_InvalidTokenInListClearsField)
{
    load.specType = "draft-mtp,bogus";
    load.validate();
    EXPECT_EQ(load.specType, "");
}

TEST_F(ConfigValidationTest, LoadSettings_SpecType_DflashPreserved)
{
    load.specType = "draft-dflash";
    load.validate();
    EXPECT_EQ(load.specType, "draft-dflash");
}
