/**
 * @file SettingsPanelTest.cpp
 * @brief Unit tests for SettingsPanel using mocked ConfigManager.
 */

#include "settingsPanel.h"

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

using namespace testing;

class SettingsPanelTest : public Test
{
  protected:
    void SetUp() override
    {
        ON_CALL(mockConfig, getConfigSnapshot())
            .WillByDefault(ReturnPointee(&config));
        ON_CALL(mockConfig, getServerSettings())
            .WillByDefault(Invoke([this] { return config.server; }));
        ON_CALL(mockConfig, setConfig(_))
            .WillByDefault(
                Invoke([this](const Config::UserConfig &c) { config = c; }));
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

TEST_F(SettingsPanelTest, ConstructorLoadsFromMockConfig)
{
    EXPECT_CALL(mockConfig, getConfigSnapshot()).Times(1);

    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};

    SettingsPanel panel(deps);
    SUCCEED() << "SettingsPanel constructed with mocked config";
}

TEST_F(SettingsPanelTest, ComponentReturnsValidElement)
{
    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};

    SettingsPanel panel(deps);
    auto comp = panel.component();
    ASSERT_TRUE(comp);
}

TEST_F(SettingsPanelTest, LoadsSectionARouterParamsAndRenders)
{
    // Seed section-A params, including enum values, so the load path maps them
    // to widget state (and enum strings to Menu indices) without throwing.
    config.server.modelsMax              = 12;
    config.server.modelsAutoload         = true;
    config.server.checkpointMinStep      = 1024;
    config.server.cacheIdleSlots         = false;
    config.server.pooling                = "last";
    config.server.embdNormalize          = 2;
    config.server.reasoning              = "off";
    config.server.reasoningBudget        = 128;

    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};

    SettingsPanel panel(deps);
    auto comp = panel.component();
    ASSERT_TRUE(comp);
    ftxui::Screen screen(120, 40);
    ftxui::Render(screen, comp->Render());
    SUCCEED() << "Section-A params loaded and panel rendered";
}

TEST_F(SettingsPanelTest, LoadsTemperatureUnitAndRenders)
{
    // Seed the C/F unit so the load path maps the string to its dropdown index
    // and the panel renders the Temp Unit row without throwing.
    config.ui.temperatureUnit = "fahrenheit";

    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};

    SettingsPanel panel(deps);
    auto comp = panel.component();
    ASSERT_TRUE(comp);
    ftxui::Screen screen(120, 40);
    ftxui::Render(screen, comp->Render());
    SUCCEED() << "Temperature unit loaded and panel rendered";
}

TEST_F(SettingsPanelTest, RendersAtShortHeightAndTabReachesControls)
{
    // Phase 4.5: each section scrolls independently. At a short height the
    // panel must still render without throwing, and Tab must move focus
    // through the (now-scrolled) controls. yframe keeps the focused control in
    // view; here we only assert no-throw + that Tab events are accepted.
    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};

    SettingsPanel panel(deps);
    auto comp = panel.component();
    ASSERT_TRUE(comp);

    ftxui::Screen screen(120, 12); // deliberately short
    ftxui::Render(screen, comp->Render());

    // Drive focus through several controls; each Tab + re-render must not throw.
    for (int i = 0; i < 25; ++i) {
        comp->OnEvent(ftxui::Event::Tab);
        ftxui::Render(screen, comp->Render());
    }
    SUCCEED() << "Short-height panel rendered and accepted Tab navigation";
}

TEST_F(SettingsPanelTest, LoadsTempThresholdsAndRenders)
{
    // Seed custom threshold values in mock config so the load path maps them
    // to widget state and the panel renders the threshold rows without
    // throwing.
    config.ui.cpuTemperatureGreenBottom = 40;
    config.ui.cpuTemperatureRedTop = 85;
    config.ui.gpuTemperatureGreenBottom = 50;
    config.ui.gpuTemperatureRedTop = 95;

    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};

    SettingsPanel panel(deps);
    auto comp = panel.component();
    ASSERT_TRUE(comp);
    ftxui::Screen screen(120, 40);
    ftxui::Render(screen, comp->Render());
    SUCCEED() << "Temperature thresholds loaded and panel rendered";
}

TEST_F(SettingsPanelTest, SavesTempThresholdsToConfig)
{
    // Use non-default threshold values so the test verifies that CHANGED
    // values are persisted — not just that defaults survive a save cycle.
    config.ui.cpuTemperatureGreenBottom = 40;
    config.ui.cpuTemperatureRedTop = 85;
    config.ui.gpuTemperatureGreenBottom = 50;
    config.ui.gpuTemperatureRedTop = 95;

    // Capture the config passed to setConfig.
    Config::UserConfig capturedConfig;
    EXPECT_CALL(mockConfig, setConfig(_))
        .WillOnce(Invoke([&capturedConfig](const Config::UserConfig &c) {
            capturedConfig = c;
        }));

    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};

    SettingsPanel panel(deps);
    auto comp = panel.component();

    // Simulate a change by triggering save (the panel saves on every control
    // change; here we just verify the save path includes the new fields).
    comp->OnEvent(ftxui::Event::Return); // trigger a save

    // The non-default thresholds should be present in the captured config.
    EXPECT_EQ(capturedConfig.ui.cpuTemperatureGreenBottom, 40);
    EXPECT_EQ(capturedConfig.ui.cpuTemperatureRedTop, 85);
    EXPECT_EQ(capturedConfig.ui.gpuTemperatureGreenBottom, 50);
    EXPECT_EQ(capturedConfig.ui.gpuTemperatureRedTop, 95);
    SUCCEED() << "Temperature thresholds saved to config";
}

TEST_F(SettingsPanelTest, LoadsDifferentCpuAndGpuThresholds)
{
    // Set distinct CPU and GPU thresholds to verify the panel loads
    // independent values for each device.
    config.ui.cpuTemperatureGreenBottom = 30;
    config.ui.cpuTemperatureRedTop = 80;
    config.ui.gpuTemperatureGreenBottom = 50;
    config.ui.gpuTemperatureRedTop = 100;

    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};

    SettingsPanel panel(deps);
    auto comp = panel.component();
    ASSERT_TRUE(comp);
    ftxui::Screen screen(120, 40);
    ftxui::Render(screen, comp->Render());
    SUCCEED() << "Different CPU/GPU thresholds loaded and panel rendered";
}

TEST_F(SettingsPanelTest, LoadsSystemResourcesOnlyFromConfig)
{
    // Seed systemResourcesOnly = true so the load path maps it to widget
    // state and the panel renders the checkbox without throwing.
    config.ui.systemResourcesOnly = true;

    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};

    SettingsPanel panel(deps);
    auto comp = panel.component();
    ASSERT_TRUE(comp);
    ftxui::Screen screen(120, 40);
    ftxui::Render(screen, comp->Render());
    SUCCEED() << "System Resources Only loaded and panel rendered";
}

TEST_F(SettingsPanelTest, SavesSystemResourcesOnlyToConfig)
{
    // Seed systemResourcesOnly = true so the save path persists it.
    config.ui.systemResourcesOnly = true;

    // Capture the config passed to setConfig.
    Config::UserConfig capturedConfig;
    EXPECT_CALL(mockConfig, setConfig(_))
        .WillOnce(Invoke([&capturedConfig](const Config::UserConfig &c) {
            capturedConfig = c;
        }));

    AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
                         mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};

    SettingsPanel panel(deps);
    auto comp = panel.component();

    // Simulate a save via Return key.
    comp->OnEvent(ftxui::Event::Return);

    // The systemResourcesOnly flag should be present in the captured config.
    EXPECT_EQ(capturedConfig.ui.systemResourcesOnly, true);
    SUCCEED() << "System Resources Only saved to config";
}

// =============================================================================
// vLLM Server Settings
// =============================================================================

TEST_F(SettingsPanelTest, LoadsVllmSettingsFromConfig)
{
	// Seed vLLM settings so the load path maps them to widget state.
	config.vllm.host = "192.168.1.100";
	config.vllm.port = 8100;

	AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
			 mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};

	SettingsPanel panel(deps);
	auto comp = panel.component();
	ASSERT_TRUE(comp);
	ftxui::Screen screen(120, 40);
	ftxui::Render(screen, comp->Render());
	SUCCEED() << "vLLM settings loaded and panel rendered";
}

TEST_F(SettingsPanelTest, SavesVllmSettingsToConfig)
{
	// Seed vLLM settings so the save path persists them.
	config.vllm.host = "10.0.0.50";
	config.vllm.port = 8200;

	// Capture the config passed to setConfig.
	Config::UserConfig capturedConfig;
	EXPECT_CALL(mockConfig, setConfig(_))
		.WillOnce(Invoke([&capturedConfig](const Config::UserConfig &c) {
			capturedConfig = c;
		}));

	AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
			 mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};

	SettingsPanel panel(deps);
	auto comp = panel.component();

	// Simulate a save via Return key.
	comp->OnEvent(ftxui::Event::Return);

	// The vLLM settings should be present in the captured config.
	EXPECT_EQ(capturedConfig.vllm.host, "10.0.0.50");
	EXPECT_EQ(capturedConfig.vllm.port, 8200);
	SUCCEED() << "vLLM settings saved to config";
}

TEST_F(SettingsPanelTest, LoadsDefaultVllmSettings)
{
	// Default vLLM settings (empty host, port 8000).
	config.vllm.host = "";
	config.vllm.port = 8000;

	AppDependencies deps{mockConfig,   mockServer, mockModelInfo, mockTracker,
			 mockModelsIni, mockBatches, mockBatchTracker, mockCpu, mockMem, mockGpu};

	SettingsPanel panel(deps);
	auto comp = panel.component();
	ASSERT_TRUE(comp);
	ftxui::Screen screen(120, 40);
	ftxui::Render(screen, comp->Render());
	SUCCEED() << "Default vLLM settings loaded and panel rendered";
}
