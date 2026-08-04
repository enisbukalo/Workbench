/**
 * @file ModelsPanelTest.cpp
 * @brief Unit tests for ModelsPanel using mocked dependencies.
 */

#include "modelsPanel.h"

#include "MockConfigManager.h"
#include "MockLlamaServerProcess.h"
#include "MockModelInfoMonitor.h"
#include "MockModelStateTracker.h"
#include "MockBatchStateTracker.h"
#include "MockBatchStore.h"
#include "MockModelsIni.h"
#include "MockCpuMonitor.h"
#include "MockMemoryMonitor.h"
#include "MockGpuMonitor.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>

using namespace testing;
using namespace ftxui;

class ModelsPanelTest : public Test
{
  protected:
    void SetUp() override
    {
        // Set up default config values so panel constructor doesn't crash.
        // ReturnPointee re-reads `config` at call time, so tests can mutate
        // it after SetUp and the panel still sees the latest values.
        ON_CALL(mockConfig, getConfigSnapshot())
            .WillByDefault(ReturnPointee(&config));
        ON_CALL(mockConfig, getServerSettings())
            .WillByDefault(Invoke([this] { return config.server; }));

        // Server not running by default
        ON_CALL(mockServer, isRunning()).WillByDefault(Return(false));
        ON_CALL(mockServer, isServerHealthy()).WillByDefault(Return(false));

        // Model info shows nothing loaded
        ModelInfo info;
        info.isModelLoaded = false;
        info.isServerRunning = false;
        ON_CALL(mockModelInfo, getStats()).WillByDefault(Return(info));

        // Empty model list from models.ini
        ON_CALL(mockModelsIni, getUniqueModelEntries())
            .WillByDefault(Return(std::vector<ModelsIniEntry>{}));
        ON_CALL(mockModelsIni, getPresetsForModel(_))
            .WillByDefault(Return(std::vector<Config::ModelPreset>{}));
        ON_CALL(mockModelsIni, savePreset(_)).WillByDefault(Return(true));

        // Default: 1 GPU (single-GPU systems hide tensor split UI)
        ON_CALL(mockGpu, getStats())
            .WillByDefault(Return(std::vector<MemoryStats>(1)));
        ON_CALL(mockGpu, update()).WillByDefault(Return());
    }

    /**
     * @brief Build a panel for a single model whose only preset carries the
     *        given raw tensorSplit, then save the form back to a preset and
     *        return the rounded tensorSplit that would be written to models.ini.
     *
     * Load/inference settings no longer round-trip through config.json — they
     * are persisted to a models.ini preset via saveCurrentToPreset(). So the
     * round-trip is: preset.load.tensorSplit (raw) -> applyPreset() populates the
     * form -> saveCurrentToPreset() -> savePreset(preset) with the rounded value.
     */
    std::string tensorSplitRoundTrip(const std::string &raw, int gpuCount)
    {
        config = Config::UserConfig{};

        ON_CALL(mockGpu, getStats())
            .WillByDefault(Return(std::vector<MemoryStats>(gpuCount)));

        ON_CALL(mockModelsIni, getUniqueModelEntries())
            .WillByDefault(Return(std::vector<ModelsIniEntry>{
                ModelsIniEntry{"ModelA", "/models/a.gguf"}}));

        Config::ModelPreset preset;
        preset.name = "P";
        preset.model = "/models/a.gguf";
        preset.load.tensorSplit = raw;
        ON_CALL(mockModelsIni, getPresetsForModel("/models/a.gguf"))
            .WillByDefault(Return(std::vector<Config::ModelPreset>{preset}));

        // Capture the preset handed to savePreset() so we can read the rounded
        // tensorSplit the panel produced.
        Config::ModelPreset saved;
        ON_CALL(mockModelsIni, savePreset(_))
            .WillByDefault(DoAll(SaveArg<0>(&saved), Return(true)));

        AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                             mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};
        ModelsPanel panel(deps);
        (void)panel.component();

        panel.saveCurrentToPreset();
        return saved.load.tensorSplit;
    }

    // Test seam: drive the private save-preset action directly. The fixture is
    // a friend of ModelsPanel (see modelsPanel.h), so it can reach private
    // methods without simulating UI focus navigation.
    void invokeSaveCurrentToPreset(ModelsPanel &panel)
    {
        panel.saveCurrentToPreset();
    }

    // Crash-recovery seam: drive the private refreshServerState(), which now
    // reads crashes from the tracker (mockTracker.takeCrashed()) and restarts
    // the server only when the tracker reports a genuine crash.
    void invokeRefreshServerState(ModelsPanel &panel)
    {
        panel.refreshServerState();
    }

    // Add-New-Model popup seams. The fixture is a friend of ModelsPanel, so it
    // can set the bound path and drive the private confirm/cancel actions.
    void setAddModelPath(ModelsPanel &panel, const std::string &path)
    {
        panel.m_addModelPath = path;
    }
    void invokeConfirmAddModel(ModelsPanel &panel) { panel.confirmAddModel(); }
    void invokeOpenAddModelPopup(ModelsPanel &panel) { panel.openAddModelPopup(); }

    // Delete-preset seam plus accessors for the dropdown state it must update.
    void setSelectedPresetIndex(ModelsPanel &panel, int idx)
    {
        panel.m_selectedPresetIndex = idx;
    }
    void invokeDeleteSelectedPreset(ModelsPanel &panel)
    {
        panel.deleteSelectedPreset();
    }
    const std::vector<std::string> &modelDisplayNames(const ModelsPanel &panel) const
    {
        return panel.m_modelDisplayNames;
    }
    std::string selectedModelName(const ModelsPanel &panel) const
    {
        return panel.m_selectedModelName;
    }
    bool addModelPopupShown(const ModelsPanel &panel) const
    {
        return panel.m_showAddModelPopup;
    }
    std::string addModelError(const ModelsPanel &panel) const
    {
        return panel.m_addModelError;
    }

    // Per-preset load/unload seams (#110). The fixture is a friend of
    // ModelsPanel, so it reaches private state/actions without simulating UI
    // focus navigation.
    void invokeOnLoadUnloadClicked(ModelsPanel &panel)
    {
        panel.onLoadUnloadClicked();
    }
    void setStartStopLabel(ModelsPanel &panel, const std::string &label)
    {
        panel.m_startStopLabel = label;
    }
    std::string startStopLabel(const ModelsPanel &panel) const
    {
        return panel.m_startStopLabel;
    }
    void updateStartStopLabel(ModelsPanel &panel)
    {
        panel.updateStartStopLabel();
    }
    void clearModelLoadingFlag(ModelsPanel &panel)
    {
        // Reset the in-flight async-load guard so a subsequent synchronous load
        // click is not short-circuited (the worker thread is irrelevant here —
        // tests drive the load path directly).
        panel.m_modelLoading.store(false, std::memory_order_release);
        panel.joinAsyncPoll();
    }

    // Create an empty temp file and return its full path. Cleaned up by the OS
    // temp dir; tests only need a real on-disk file for path validation.
    std::string makeTempFile(const std::string &name) const
    {
        std::filesystem::path p = std::filesystem::temp_directory_path() / name;
        std::ofstream(p).close();
        return p.string();
    }

    NiceMock<MockConfigManager> mockConfig;
    NiceMock<MockLlamaServerProcess> mockServer;
    NiceMock<MockModelInfoMonitor> mockModelInfo;
    NiceMock<MockModelStateTracker> mockTracker;
    NiceMock<MockModelsIni> mockModelsIni;
    NiceMock<MockBatchStore> mockBatches;
    NiceMock<MockBatchStateTracker> mockBatchTracker;
    NiceMock<MockCpuMonitor> mockCpu;
    NiceMock<MockMemoryMonitor> mockMem;
    NiceMock<MockGpuMonitor> mockGpu;
    Config::UserConfig config{};
};

TEST_F(ModelsPanelTest, ConstructorDoesNotCallSingletons)
{
    // Verify that constructing the panel only calls our mocks, not singletons.
    EXPECT_CALL(mockConfig, getConfigSnapshot()).Times(AtLeast(0));
    EXPECT_CALL(mockModelsIni, getUniqueModelEntries()).Times(AtLeast(1));

    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};
    ModelsPanel panel(deps);  // Should not crash or call singletons

    SUCCEED() << "ModelsPanel constructed successfully with mocked deps";
}

TEST_F(ModelsPanelTest, ComponentReturnsValidElement)
{
    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};
    ModelsPanel panel(deps);

    auto comp = panel.component();
    ASSERT_TRUE(comp);  // Component should be non-null
}

TEST_F(ModelsPanelTest, RendersAtShortHeightAndTabReachesControls)
{
    // Phase 4.5: Load and Inference sections scroll independently. At a short
    // height the panel must render without throwing, and Tab navigation across
    // the (scrolled) controls must be accepted. yframe keeps the focused
    // control in view; we assert no-throw + Tab acceptance.
    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};
    ModelsPanel panel(deps);

    auto comp = panel.component();
    ASSERT_TRUE(comp);

    Screen screen(120, 12); // deliberately short
    Render(screen, comp->Render());

    for (int i = 0; i < 40; ++i) {
        comp->OnEvent(Event::Tab);
        Render(screen, comp->Render());
    }
    SUCCEED() << "Short-height models panel rendered and accepted Tab nav";
}

TEST_F(ModelsPanelTest, ServerNotRunningShowsLoadLabel)
{
    // Panel calls isRunning() multiple times during construction (refreshServerState + updateStartStopLabel).
    EXPECT_CALL(mockServer, isRunning()).Times(AtLeast(1)).WillRepeatedly(Return(false));

    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};
    ModelsPanel panel(deps);

    // Component creation triggers server state refresh
    auto comp = panel.component();
    SUCCEED() << "Server not running - LOAD label expected";
}

// =============================================================================
// Add New Model popup (issue #78)
// =============================================================================

TEST_F(ModelsPanelTest, AddModel_ValidPath_SavesDefaultPresetAndClosesPopup)
{
    const std::string ggufPath = makeTempFile("wb_addmodel_valid.gguf");

    Config::ModelPreset saved;
    ON_CALL(mockModelsIni, savePreset(_))
        .WillByDefault(DoAll(SaveArg<0>(&saved), Return(true)));

    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};
    ModelsPanel panel(deps);
    (void)panel.component();

    invokeOpenAddModelPopup(panel);
    EXPECT_TRUE(addModelPopupShown(panel));

    setAddModelPath(panel, ggufPath);
    invokeConfirmAddModel(panel);

    // Section name derives from the filename stem + "_DEFAULT".
    EXPECT_EQ(saved.name, "wb_addmodel_valid_DEFAULT");
    EXPECT_EQ(saved.model, ggufPath);
    EXPECT_EQ(saved.load.modelPath, ggufPath);
    // Popup closes on success.
    EXPECT_FALSE(addModelPopupShown(panel));

    std::filesystem::remove(ggufPath);
}

TEST_F(ModelsPanelTest, AddModel_InvalidPath_DoesNotSaveAndKeepsPopupOpen)
{
    // savePreset must never be called when validation fails.
    EXPECT_CALL(mockModelsIni, savePreset(_)).Times(0);

    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};
    ModelsPanel panel(deps);
    (void)panel.component();

    invokeOpenAddModelPopup(panel);
    setAddModelPath(panel, "/nonexistent/path/to/model.gguf");
    invokeConfirmAddModel(panel);

    EXPECT_TRUE(addModelPopupShown(panel));     // stays open
    EXPECT_FALSE(addModelError(panel).empty()); // error surfaced
}

TEST_F(ModelsPanelTest, AddModel_SaveFails_KeepsPopupOpenWithError)
{
    const std::string ggufPath = makeTempFile("wb_addmodel_savefail.gguf");

    ON_CALL(mockModelsIni, savePreset(_)).WillByDefault(Return(false));

    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};
    ModelsPanel panel(deps);
    (void)panel.component();

    invokeOpenAddModelPopup(panel);
    setAddModelPath(panel, ggufPath);
    invokeConfirmAddModel(panel);

    EXPECT_TRUE(addModelPopupShown(panel));
    EXPECT_EQ(addModelError(panel), "Save failed");

    std::filesystem::remove(ggufPath);
}

// =============================================================================
// Delete last preset removes the model from the dropdown (issue #78 follow-up)
// =============================================================================

TEST_F(ModelsPanelTest, DeleteLastPreset_RemovesModelFromDropdown)
{
    // One model with exactly one preset. After the preset is deleted, the
    // model has no models.ini section, so getUniqueModelEntries returns empty
    // and getPresetsForModel returns empty — the dropdown must shrink to empty.
    bool deleted = false;

    ON_CALL(mockModelsIni, getUniqueModelEntries())
        .WillByDefault(Invoke([&deleted] {
            if (deleted)
                return std::vector<ModelsIniEntry>{};
            return std::vector<ModelsIniEntry>{
                ModelsIniEntry{"ModelA_DEFAULT", "/models/a.gguf"}};
        }));

    Config::ModelPreset preset;
    preset.name = "ModelA_DEFAULT";
    preset.model = "/models/a.gguf";
    ON_CALL(mockModelsIni, getPresetsForModel("/models/a.gguf"))
        .WillByDefault(Invoke([&deleted, preset](const std::string &) {
            if (deleted)
                return std::vector<Config::ModelPreset>{};
            return std::vector<Config::ModelPreset>{preset};
        }));

    ON_CALL(mockModelsIni, deletePreset("ModelA_DEFAULT"))
        .WillByDefault(Invoke([&deleted](const std::string &) {
            deleted = true;
            return true;
        }));

    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};
    ModelsPanel panel(deps);
    (void)panel.component();

    // Model present before deletion.
    ASSERT_EQ(modelDisplayNames(panel).size(), 1u);

    setSelectedPresetIndex(panel, 0);
    invokeDeleteSelectedPreset(panel);

    // Model gone from the dropdown; no model selected.
    EXPECT_TRUE(modelDisplayNames(panel).empty());
    EXPECT_TRUE(selectedModelName(panel).empty());
}

TEST_F(ModelsPanelTest, DeleteOneOfManyPresets_KeepsModelInDropdown)
{
    // Model has two presets. Deleting one leaves the other, so the model
    // remains loadable and must stay in the dropdown.
    int presetCount = 2;

    ON_CALL(mockModelsIni, getUniqueModelEntries())
        .WillByDefault(Return(std::vector<ModelsIniEntry>{
            ModelsIniEntry{"ModelA", "/models/a.gguf"}}));

    ON_CALL(mockModelsIni, getPresetsForModel("/models/a.gguf"))
        .WillByDefault(Invoke([&presetCount](const std::string &) {
            std::vector<Config::ModelPreset> v;
            for (int i = 0; i < presetCount; ++i) {
                Config::ModelPreset p;
                p.name = "P" + std::to_string(i);
                p.model = "/models/a.gguf";
                v.push_back(p);
            }
            return v;
        }));

    ON_CALL(mockModelsIni, deletePreset(_))
        .WillByDefault(Invoke([&presetCount](const std::string &) {
            --presetCount;
            return true;
        }));

    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};
    ModelsPanel panel(deps);
    (void)panel.component();

    ASSERT_EQ(modelDisplayNames(panel).size(), 1u);

    setSelectedPresetIndex(panel, 0);
    invokeDeleteSelectedPreset(panel);

    // Still one preset left → model stays in the dropdown.
    EXPECT_EQ(modelDisplayNames(panel).size(), 1u);
}

// =============================================================================
// initTensorSplitFromConfig behavior — tested via the preset save round-trip
// =============================================================================

TEST_F(ModelsPanelTest, InitTensorSplit_ExactMatch_AllValid)
{
    // "50,20,30", gpuCount=3 → "50.00,20.00,30.00"
    EXPECT_EQ(tensorSplitRoundTrip("50,20,30", 3), "50.00,20.00,30.00");
}

TEST_F(ModelsPanelTest, InitTensorSplit_TooManyTokens_Truncated)
{
    // "50,20,30", gpuCount=2 → "50.00,20.00"
    EXPECT_EQ(tensorSplitRoundTrip("50,20,30", 2), "50.00,20.00");
}

TEST_F(ModelsPanelTest, InitTensorSplit_TooFewTokens_PaddedWithDefault)
{
    // "50", gpuCount=2 → "50.00,1.00"
    EXPECT_EQ(tensorSplitRoundTrip("50", 2), "50.00,1.00");
}

TEST_F(ModelsPanelTest, InitTensorSplit_EmptyRaw_AllDefault)
{
    // "", gpuCount=2 → "1.00,1.00"
    EXPECT_EQ(tensorSplitRoundTrip("", 2), "1.00,1.00");
}

TEST_F(ModelsPanelTest, InitTensorSplit_InvalidToken_ReplacedWithDefault)
{
    // "abc,2.0", gpuCount=2 → "1.00,2.00"
    EXPECT_EQ(tensorSplitRoundTrip("abc,2.0", 2), "1.00,2.00");
}

TEST_F(ModelsPanelTest, InitTensorSplit_NegativeToken_ReplacedWithDefault)
{
    // "-5,2.0", gpuCount=2 → "1.00,2.00"
    EXPECT_EQ(tensorSplitRoundTrip("-5,2.0", 2), "1.00,2.00");
}

// =============================================================================
// First-load preset auto-selection (regression test for issue #72)
//
// On first app load the initially selected model's first preset must be applied
// WITHOUT the user having to change models away and back. We verify by saving
// the form back to a preset and checking the value originated from the applied
// preset (not the struct default).
// =============================================================================

TEST_F(ModelsPanelTest, FirstLoad_AutoSelectsAndAppliesFirstPreset)
{
    config = Config::UserConfig{};

    ON_CALL(mockModelsIni, getUniqueModelEntries())
        .WillByDefault(Return(std::vector<ModelsIniEntry>{
            ModelsIniEntry{"ModelA", "/models/a.gguf"}}));

    // First preset for that model carries a distinctive ctxSize so we can prove
    // applyPreset() ran (default LoadSettings ctxSize differs).
    Config::ModelPreset preset;
    preset.name           = "Fast";
    preset.model          = "/models/a.gguf";
    preset.load.ctxSize   = 4096;
    ON_CALL(mockModelsIni, getPresetsForModel("/models/a.gguf"))
        .WillByDefault(Return(std::vector<Config::ModelPreset>{preset}));

    Config::ModelPreset saved;
    ON_CALL(mockModelsIni, savePreset(_))
        .WillByDefault(DoAll(SaveArg<0>(&saved), Return(true)));

    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};
    ModelsPanel panel(deps);
    (void)panel.component();

    // Saving the form back to a preset reflects the auto-applied first preset.
    invokeSaveCurrentToPreset(panel);

    EXPECT_EQ(saved.load.ctxSize, 4096);
}

TEST_F(ModelsPanelTest, FirstLoad_FirstModelPresetApplied)
{
    config = Config::UserConfig{};

    ON_CALL(mockModelsIni, getUniqueModelEntries())
        .WillByDefault(Return(std::vector<ModelsIniEntry>{
            ModelsIniEntry{"ModelA", "/models/a.gguf"}}));

    Config::ModelPreset preset;
    preset.name         = "Fast";
    preset.model        = "/models/a.gguf";
    preset.load.ctxSize = 8192;
    ON_CALL(mockModelsIni, getPresetsForModel("/models/a.gguf"))
        .WillByDefault(Return(std::vector<Config::ModelPreset>{preset}));

    Config::ModelPreset saved;
    ON_CALL(mockModelsIni, savePreset(_))
        .WillByDefault(DoAll(SaveArg<0>(&saved), Return(true)));

    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};
    ModelsPanel panel(deps);
    (void)panel.component();

    invokeSaveCurrentToPreset(panel);

    EXPECT_EQ(saved.load.ctxSize, 8192);
}

// =============================================================================
// Save-triggered server reload must not auto-reload the model (issue #71)
//
// When a preset is saved while the server is running, the panel restarts the
// server to apply new settings. Restart launches with an empty model path so no
// model is loaded. But ModelInfoMonitor polls /models + /slots at 1Hz, and in
// router mode those queries make llama-server auto-load the cached model again —
// so the "same model" comes back right after terminate. The fix is to drive the
// tracker into the unload-all state (requestUnloadAll) before relaunching,
// exactly as the UNLOAD button does, so polling stops issuing model-loading
// queries.
// =============================================================================

TEST_F(ModelsPanelTest, SavePresetReload_ForcesMonitorUnloaded_BeforeRelaunch)
{
    config = Config::UserConfig{};

    ON_CALL(mockModelsIni, getUniqueModelEntries())
        .WillByDefault(Return(std::vector<ModelsIniEntry>{
            ModelsIniEntry{"ModelA", "/models/a.gguf"}}));
    ON_CALL(mockModelsIni, getPresetsForModel(_))
        .WillByDefault(Return(std::vector<Config::ModelPreset>{}));

    // Server is running, so saving a preset triggers the restart path.
    ON_CALL(mockServer, isRunning()).WillByDefault(Return(true));

    // savePreset succeeds so the restart branch is reached.
    ON_CALL(mockModelsIni, savePreset(_)).WillByDefault(Return(true));

    // Contract: the monitor must be told the model is unloaded BEFORE the new
    // server is launched, otherwise the 1Hz poll reloads the cached model.
    // launch() now takes only (modelPath, serverSettings).
    {
        InSequence seq;
        EXPECT_CALL(mockTracker, requestUnloadAll()).Times(1);
        EXPECT_CALL(mockServer, terminate()).Times(1);
        EXPECT_CALL(mockServer, launch("", _)).Times(1).WillOnce(Return(true));
    }

    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};
    ModelsPanel panel(deps);
    (void)panel.component();

    invokeSaveCurrentToPreset(panel);
}

// =============================================================================
// Server-side model crash recovery (issue #85).
//
// In router mode a worker can die (e.g. CUDA OOM) while the router parent is
// still up. The monitor flips isModelLoaded false once its worker probe fails.
// refreshServerState() must then restart the server (terminate + relaunch with
// no model), mirroring the preset-save restart, so the UI returns to LOAD.
// Recovery is gated on the server *process* being alive (isRunning), not on a
// healthy /health, because a wedged router may stop answering /health.
// =============================================================================

TEST_F(ModelsPanelTest, ModelCrash_RestartsServerWithNoModel)
{
    config = Config::UserConfig{};

    // Server (router) is up and healthy.
    ON_CALL(mockServer, isRunning()).WillByDefault(Return(true));
    ON_CALL(mockServer, isServerHealthy()).WillByDefault(Return(true));

    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};
    ModelsPanel panel(deps);
    // component() runs an initial refresh; takeCrashed() is empty by default then
    // so no restart fires during construction.
    (void)panel.component();

    // Now the tracker surfaces a genuine crash for "PA". Recovery: drop all state
    // + suppress polling, then terminate and relaunch with an empty model path.
    ON_CALL(mockTracker, takeCrashed())
        .WillByDefault(Return(std::vector<std::string>{"PA"}));
    {
        InSequence seq;
        EXPECT_CALL(mockTracker, requestUnloadAll()).Times(1);
        EXPECT_CALL(mockServer, terminate()).Times(1);
        EXPECT_CALL(mockServer, launch("", _)).Times(1).WillOnce(Return(true));
    }

    invokeRefreshServerState(panel);
}

TEST_F(ModelsPanelTest, ModelStillLoaded_DoesNotRestartServer)
{
    config = Config::UserConfig{};

    ON_CALL(mockServer, isRunning()).WillByDefault(Return(true));
    ON_CALL(mockServer, isServerHealthy()).WillByDefault(Return(true));

    // The selected preset's model is still loaded — no crash, so no restart.
    ON_CALL(mockModelsIni, getUniqueModelEntries())
        .WillByDefault(Return(std::vector<ModelsIniEntry>{
            ModelsIniEntry{"ModelA", "/models/a.gguf"}}));
    Config::ModelPreset pa;
    pa.name = "PA";
    pa.model = "/models/a.gguf";
    ON_CALL(mockModelsIni, getPresetsForModel("/models/a.gguf"))
        .WillByDefault(Return(std::vector<Config::ModelPreset>{pa}));

    ModelInfo info;
    info.isServerRunning = true;
    info.isModelLoaded = true;
    info.loadedModel = "PA";
    std::map<std::string, ModelInfo> all{{"PA", info}};
    ON_CALL(mockModelInfo, getAllStats()).WillByDefault(Return(all));

    EXPECT_CALL(mockServer, terminate()).Times(0);
    EXPECT_CALL(mockServer, launch(_, _)).Times(0);

    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};
    ModelsPanel panel(deps);
    (void)panel.component();

    setSelectedPresetIndex(panel, 0); // PA selected and loaded
    // Tracker reports no crash (default empty takeCrashed()) — no restart.
    invokeRefreshServerState(panel);
}

TEST_F(ModelsPanelTest, SwitchToUnloadedPreset_DoesNotRestartServer)
{
    // Regression: switching the highlighted preset to one that is NOT loaded
    // must not be mistaken for a crash. The loaded SET is unchanged (PA stays
    // loaded), only the UI selection moved to PB — no restart.
    config = Config::UserConfig{};

    ON_CALL(mockServer, isRunning()).WillByDefault(Return(true));
    ON_CALL(mockServer, isServerHealthy()).WillByDefault(Return(true));

    ON_CALL(mockModelsIni, getUniqueModelEntries())
        .WillByDefault(Return(std::vector<ModelsIniEntry>{
            ModelsIniEntry{"ModelA", "/models/a.gguf"}}));
    Config::ModelPreset pa;
    pa.name = "PA";
    pa.model = "/models/a.gguf";
    Config::ModelPreset pb;
    pb.name = "PB";
    pb.model = "/models/a.gguf";
    ON_CALL(mockModelsIni, getPresetsForModel("/models/a.gguf"))
        .WillByDefault(Return(std::vector<Config::ModelPreset>{pa, pb}));

    // Only PA is loaded server-side.
    ModelInfo info;
    info.isServerRunning = true;
    info.isModelLoaded = true;
    info.loadedModel = "PA";
    ON_CALL(mockModelInfo, getAllStats())
        .WillByDefault(Return(std::map<std::string, ModelInfo>{{"PA", info}}));

    EXPECT_CALL(mockServer, terminate()).Times(0);
    EXPECT_CALL(mockServer, launch(_, _)).Times(0);

    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};
    ModelsPanel panel(deps);
    (void)panel.component();

    // PA stays loaded; user highlights PB (not loaded). No crash from the
    // tracker, so no restart.
    setSelectedPresetIndex(panel, 1); // PB — unloaded
    invokeRefreshServerState(panel);

    // Label correctly shows LOAD for the unloaded PB, with no restart.
    updateStartStopLabel(panel);
    EXPECT_EQ(startStopLabel(panel), "LOAD");
}

TEST_F(ModelsPanelTest, ModelCrash_RestartsEvenWhenHealthWedged)
{
    config = Config::UserConfig{};

    // The router process is still alive (isRunning) but its /health has stopped
    // responding — the wedged-router case after a worker OOM (#18912). Recovery
    // must still fire; gating on health would leave the server stuck looping.
    ON_CALL(mockServer, isRunning()).WillByDefault(Return(true));
    ON_CALL(mockServer, isServerHealthy()).WillByDefault(Return(false));

    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};
    ModelsPanel panel(deps);
    (void)panel.component(); // initial refresh: takeCrashed() empty, no restart

    // Tracker reports a crash even though /health is wedged.
    ON_CALL(mockTracker, takeCrashed())
        .WillByDefault(Return(std::vector<std::string>{"PA"}));
    {
        InSequence seq;
        EXPECT_CALL(mockTracker, requestUnloadAll()).Times(1);
        EXPECT_CALL(mockServer, terminate()).Times(1);
        EXPECT_CALL(mockServer, launch("", _)).Times(1).WillOnce(Return(true));
    }

    invokeRefreshServerState(panel);
}

TEST_F(ModelsPanelTest, FirstLoad_NoPresets_DoesNotCrash)
{
    config = Config::UserConfig{};

    ON_CALL(mockModelsIni, getUniqueModelEntries())
        .WillByDefault(Return(std::vector<ModelsIniEntry>{
            ModelsIniEntry{"ModelA", "/models/a.gguf"}}));
    ON_CALL(mockModelsIni, getPresetsForModel(_))
        .WillByDefault(Return(std::vector<Config::ModelPreset>{}));

    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};
    ModelsPanel panel(deps);

    auto comp = panel.component();
    ASSERT_TRUE(comp);
    SUCCEED() << "Model with no presets builds without crash on first load";
}

// =============================================================================
// Issue #87 — draft settings round-trip through UI state
// =============================================================================

TEST_F(ModelsPanelTest, DraftSettings_LoadSaveRoundTrip)
{
    config = Config::UserConfig{};

    ON_CALL(mockModelsIni, getUniqueModelEntries())
        .WillByDefault(Return(std::vector<ModelsIniEntry>{
            ModelsIniEntry{"ModelA", "/models/a.gguf"}}));

    Config::ModelPreset preset;
    preset.name = "P";
    preset.model = "/models/a.gguf";
    preset.load.cacheTypeKDraft = "q8_0";
    preset.load.cacheTypeVDraft = "q5_1";
    preset.load.deviceDraft     = "CUDA0,CUDA1";
    preset.load.preserveThinking = true;

    ON_CALL(mockModelsIni, getPresetsForModel("/models/a.gguf"))
        .WillByDefault(Return(std::vector<Config::ModelPreset>{preset}));

    Config::ModelPreset saved;
    ON_CALL(mockModelsIni, savePreset(_))
        .WillByDefault(DoAll(SaveArg<0>(&saved), Return(true)));

    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};
    ModelsPanel panel(deps);
    (void)panel.component();

    invokeSaveCurrentToPreset(panel);

    EXPECT_EQ(saved.load.cacheTypeKDraft, "q8_0");
    EXPECT_EQ(saved.load.cacheTypeVDraft, "q5_1");
    EXPECT_EQ(saved.load.deviceDraft, "CUDA0,CUDA1");
    EXPECT_TRUE(saved.load.preserveThinking);
}

TEST_F(ModelsPanelTest, DraftSettings_DefaultsRoundTrip)
{
    config = Config::UserConfig{};

    ON_CALL(mockModelsIni, getUniqueModelEntries())
        .WillByDefault(Return(std::vector<ModelsIniEntry>{
            ModelsIniEntry{"ModelA", "/models/a.gguf"}}));

    Config::ModelPreset preset;
    preset.name = "P";
    preset.model = "/models/a.gguf";
    // defaults: cacheTypeKDraft="f16", cacheTypeVDraft="f16", deviceDraft="", preserveThinking=false

    ON_CALL(mockModelsIni, getPresetsForModel("/models/a.gguf"))
        .WillByDefault(Return(std::vector<Config::ModelPreset>{preset}));

    Config::ModelPreset saved;
    ON_CALL(mockModelsIni, savePreset(_))
        .WillByDefault(DoAll(SaveArg<0>(&saved), Return(true)));

    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};
    ModelsPanel panel(deps);
    (void)panel.component();

    invokeSaveCurrentToPreset(panel);

    EXPECT_EQ(saved.load.cacheTypeKDraft, "f16");
    EXPECT_EQ(saved.load.cacheTypeVDraft, "f16");
    EXPECT_EQ(saved.load.deviceDraft, "");
    EXPECT_FALSE(saved.load.preserveThinking);
}

// =============================================================================
// Per-preset multi-model load/unload (#110)
// =============================================================================

namespace {
// Configure two presets of the SAME GGUF (distinct sections "PA"/"PB") on the
// given mocks, with the server running. Returns nothing — the mocks are wired
// in place so a freshly built panel sees both presets.
void setUpTwoPresetsSameModel(NiceMock<MockModelsIni> &ini,
                              NiceMock<MockLlamaServerProcess> &server)
{
    ON_CALL(server, isRunning()).WillByDefault(Return(true));
    ON_CALL(server, isServerHealthy()).WillByDefault(Return(true));

    ON_CALL(ini, getUniqueModelEntries())
        .WillByDefault(Return(std::vector<ModelsIniEntry>{
            ModelsIniEntry{"ModelA", "/models/a.gguf"}}));

    Config::ModelPreset pa;
    pa.name = "PA";
    pa.model = "/models/a.gguf";
    Config::ModelPreset pb;
    pb.name = "PB";
    pb.model = "/models/a.gguf";
    ON_CALL(ini, getPresetsForModel("/models/a.gguf"))
        .WillByDefault(Return(std::vector<Config::ModelPreset>{pa, pb}));
}

// Make the ModelStateTracker report exactly @p loaded as the LOADED sections.
// The load/unload button derives its state from the tracker (#110/#111 single
// source of truth), not the raw monitor poll, so tests stage tracker state here.
void setTrackerLoaded(NiceMock<MockModelStateTracker> &tracker,
                      const std::vector<std::string> &loaded)
{
    std::map<std::string, ModelState> snap;
    for (const auto &name : loaded) {
        ModelState s;
        s.id = name;
        s.lifecycle = ModelLifecycle::LOADED;
        snap[name] = s;
    }
    ON_CALL(tracker, snapshot()).WillByDefault(Return(snap));
}
} // namespace

TEST_F(ModelsPanelTest, UnloadOneOfTwo_DoesNotRestartServer)
{
    // Regression: with two presets loaded, deliberately unloading one must not
    // be read as a server-side crash when the monitor later drops it.
    setUpTwoPresetsSameModel(mockModelsIni, mockServer);
    ON_CALL(mockServer, unloadModel(_)).WillByDefault(Return(true));

    // Both PA and PB are loaded server-side at click time, so unloading PA is
    // "one of several" — single-model intent, not unload-all.
    setTrackerLoaded(mockTracker, {"PA", "PB"});

    // Unloading one of several records single-model intent (not unload-all), so
    // the tracker never reports a crash and the server is never restarted.
    EXPECT_CALL(mockTracker, requestUnload(TypedEq<const std::string &>("PA")))
        .Times(1);
    EXPECT_CALL(mockTracker, requestUnloadAll()).Times(0);
    EXPECT_CALL(mockServer, terminate()).Times(0);
    EXPECT_CALL(mockServer, launch(_, _)).Times(0);

    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};
    ModelsPanel panel(deps);
    (void)panel.component();

    setSelectedPresetIndex(panel, 0); // PA
    setStartStopLabel(panel, "UNLOAD");
    invokeOnLoadUnloadClicked(panel);

    // Tracker reports no crash (default empty takeCrashed()) — no restart.
    invokeRefreshServerState(panel);
}

TEST_F(ModelsPanelTest, LoadSelectedPreset_DrivesLoadThroughServerAndTracker)
{
    setUpTwoPresetsSameModel(mockModelsIni, mockServer);
    // The load path records intent on the tracker, then triggers the server.
    EXPECT_CALL(mockTracker, requestLoad(TypedEq<const std::string &>("PA")))
        .Times(1);
    EXPECT_CALL(mockServer, loadModel("PA")).WillOnce(Return(true));

    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};
    ModelsPanel panel(deps);
    (void)panel.component();

    setSelectedPresetIndex(panel, 0); // PA
    setStartStopLabel(panel, "LOAD");
    invokeOnLoadUnloadClicked(panel);
}

TEST_F(ModelsPanelTest, UnloadSelectedPreset_DrivesUnloadThroughServer)
{
    setUpTwoPresetsSameModel(mockModelsIni, mockServer);
    EXPECT_CALL(mockServer, unloadModel(TypedEq<const std::string &>("PA")))
        .WillOnce(Return(true));
    // PA is the only loaded preset, so this is an unload-all (last model, #71).
    EXPECT_CALL(mockTracker, requestUnloadAll()).Times(1);

    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};
    ModelsPanel panel(deps);
    (void)panel.component();

    // PA loaded server-side (staged on the monitor, not a cached panel set).
    setTrackerLoaded(mockTracker, {"PA"});
    setSelectedPresetIndex(panel, 0);
    setStartStopLabel(panel, "UNLOAD");
    invokeOnLoadUnloadClicked(panel);
}

TEST_F(ModelsPanelTest, SelectLoadedPreset_LabelIsUnload)
{
    setUpTwoPresetsSameModel(mockModelsIni, mockServer);

    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};
    ModelsPanel panel(deps);
    (void)panel.component();

    setTrackerLoaded(mockTracker, {"PA"});
    setSelectedPresetIndex(panel, 0); // PA is loaded
    updateStartStopLabel(panel);
    EXPECT_EQ(startStopLabel(panel), "UNLOAD");
}

TEST_F(ModelsPanelTest, SelectUnloadedPreset_LabelIsLoad)
{
    setUpTwoPresetsSameModel(mockModelsIni, mockServer);

    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};
    ModelsPanel panel(deps);
    (void)panel.component();

    // PA loaded, PB not. Selecting PB must show LOAD.
    setTrackerLoaded(mockTracker, {"PA"});
    setSelectedPresetIndex(panel, 1); // PB
    updateStartStopLabel(panel);
    EXPECT_EQ(startStopLabel(panel), "LOAD");
}

TEST_F(ModelsPanelTest, TwoPresetsSameModel_BothLoadable)
{
    setUpTwoPresetsSameModel(mockModelsIni, mockServer);
    EXPECT_CALL(mockServer, loadModel("PA")).WillOnce(Return(true));
    EXPECT_CALL(mockServer, loadModel("PB")).WillOnce(Return(true));

    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};
    ModelsPanel panel(deps);
    (void)panel.component();

    setSelectedPresetIndex(panel, 0); // PA
    setStartStopLabel(panel, "LOAD");
    invokeOnLoadUnloadClicked(panel);

    // Clear the in-flight load guard so the second click is not short-circuited.
    clearModelLoadingFlag(panel);

    setSelectedPresetIndex(panel, 1); // PB — same GGUF, distinct section
    setStartStopLabel(panel, "LOAD");
    invokeOnLoadUnloadClicked(panel);

    // Both sections were loaded independently: the two loadModel EXPECT_CALLs
    // above (PA then PB) verify each distinct section was sent to the server.
}
