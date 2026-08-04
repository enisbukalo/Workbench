/**
 * @file SystemResourcesPanelTest.cpp
 * @brief Unit tests for SystemResourcesPanel::render() with mocked monitors.
 */

#include "systemResourcesPanel.h"

#include "MockCpuMonitor.h"
#include "MockGpuMonitor.h"
#include "MockMemoryMonitor.h"
#include "MockModelInfoMonitor.h"
#include "MockVllmMonitor.h"

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace testing;

class SystemResourcesPanelTest : public Test
{
  protected:
    NiceMock<MockCpuMonitor> mockCpu;
    NiceMock<MockMemoryMonitor> mockMem;
    NiceMock<MockGpuMonitor> mockGpu;
    NiceMock<MockModelInfoMonitor> mockModelInfo;
    NiceMock<MockVllmMonitor> mockVllmMonitor;
};

TEST_F(SystemResourcesPanelTest, RenderWithZeroStats)
{
    // Default stats are all zeros — should not crash
    EXPECT_CALL(mockCpu, getStats()).Times(1);
    EXPECT_CALL(mockMem, getStats()).Times(1);
    EXPECT_CALL(mockGpu, getStats()).Times(1);
    EXPECT_CALL(mockGpu, getLoadStats()).Times(1);

    auto element = SystemResourcesPanel::render(mockCpu, mockMem, mockGpu, mockModelInfo);
    ASSERT_TRUE(element);  // Element should be non-null
}

TEST_F(SystemResourcesPanelTest, RenderWithCustomRamStats)
{
    MemoryStats ram;
    ram.totalMb = 32768;
    ram.usedMb = 16384;
    ram.availableMb = 16384;
    ram.usagePercentage = 50.0;

    EXPECT_CALL(mockMem, getStats()).WillOnce(Return(ram));
    EXPECT_CALL(mockCpu, getStats()).Times(1);
    EXPECT_CALL(mockGpu, getStats()).Times(1);
    EXPECT_CALL(mockGpu, getLoadStats()).Times(1);

    auto element = SystemResourcesPanel::render(mockCpu, mockMem, mockGpu, mockModelInfo);
    ASSERT_TRUE(element);
}

TEST_F(SystemResourcesPanelTest, RenderWithSingleGpu)
{
    MemoryStats vram;
    vram.id = 0;
    vram.totalMb = 24576;
    vram.usedMb = 12288;
    vram.availableMb = 12288;
    vram.usagePercentage = 50.0;

    ProcessorStats gpuLoad;
    gpuLoad.usagePercentage = 75.0;

    EXPECT_CALL(mockGpu, getStats()).WillOnce(Return(std::vector<MemoryStats>{vram}));
    EXPECT_CALL(mockGpu, getLoadStats())
        .WillOnce(Return(std::vector<ProcessorStats>{gpuLoad}));
    EXPECT_CALL(mockCpu, getStats()).Times(1);
    EXPECT_CALL(mockMem, getStats()).Times(1);

    auto element = SystemResourcesPanel::render(mockCpu, mockMem, mockGpu, mockModelInfo);
    ASSERT_TRUE(element);
}

TEST_F(SystemResourcesPanelTest, RenderWithTemperatures)
{
    // One valid CPU temperature and one GPU at the -1.0 "unavailable" sentinel:
    // the panel must render both the formatted value and the "N/A" without
    // crashing.
    ProcessorStats cpuStats;
    cpuStats.usagePercentage = 40.0;
    cpuStats.temperatureC = 65.0;

    MemoryStats vram;
    vram.id = 0;
    vram.totalMb = 24576;
    vram.usedMb = 12288;
    vram.availableMb = 12288;
    vram.usagePercentage = 50.0;

    ProcessorStats gpuLoad;
    gpuLoad.usagePercentage = 75.0;
    gpuLoad.temperatureC = -1.0; // unavailable -> "N/A"

    EXPECT_CALL(mockCpu, getStats()).WillOnce(Return(cpuStats));
    EXPECT_CALL(mockGpu, getStats()).WillOnce(Return(std::vector<MemoryStats>{vram}));
    EXPECT_CALL(mockGpu, getLoadStats())
        .WillOnce(Return(std::vector<ProcessorStats>{gpuLoad}));
    EXPECT_CALL(mockMem, getStats()).Times(1);

    auto element = SystemResourcesPanel::render(mockCpu, mockMem, mockGpu, mockModelInfo);
    ASSERT_TRUE(element);
}

TEST_F(SystemResourcesPanelTest, RenderWithMultipleGpus)
{
    std::vector<MemoryStats> vrams;
    for (int i = 0; i < 4; ++i) {
        MemoryStats s;
        s.id = i;
        s.totalMb = 24576;
        s.usedMb = 8192;
        s.availableMb = 16384;
        s.usagePercentage = 33.3;
        vrams.push_back(s);
    }

    std::vector<ProcessorStats> loads(4);
    for (int i = 0; i < 4; ++i) {
        loads[i].usagePercentage = static_cast<double>(i * 25);
    }

    EXPECT_CALL(mockGpu, getStats()).WillOnce(Return(vrams));
    EXPECT_CALL(mockGpu, getLoadStats()).WillOnce(Return(loads));
    EXPECT_CALL(mockCpu, getStats()).Times(1);
    EXPECT_CALL(mockMem, getStats()).Times(1);

    auto element = SystemResourcesPanel::render(mockCpu, mockMem, mockGpu, mockModelInfo);
    ASSERT_TRUE(element);
}

TEST_F(SystemResourcesPanelTest, RenderWithCustomTempThresholds)
{
    // Pass custom thresholds (green=40, red=90) directly to render() so the
    // parameterized interpolation path is exercised — not the ConfigManager
    // singleton defaults (30, 80).
    ProcessorStats cpuStats;
    cpuStats.usagePercentage = 40.0;
    cpuStats.temperatureC = 65.0;

    MemoryStats vram;
    vram.id = 0;
    vram.totalMb = 24576;
    vram.usedMb = 12288;
    vram.availableMb = 12288;
    vram.usagePercentage = 50.0;

    ProcessorStats gpuLoad;
    gpuLoad.usagePercentage = 75.0;
    gpuLoad.temperatureC = 55.0;

    EXPECT_CALL(mockCpu, getStats()).WillOnce(Return(cpuStats));
    EXPECT_CALL(mockGpu, getStats()).WillOnce(Return(std::vector<MemoryStats>{vram}));
    EXPECT_CALL(mockGpu, getLoadStats())
        .WillOnce(Return(std::vector<ProcessorStats>{gpuLoad}));
    EXPECT_CALL(mockMem, getStats()).Times(1);

    // Inject per-device thresholds — CPU green=40, red=80; GPU green=50, red=90.
    auto element = SystemResourcesPanel::render(mockCpu, mockMem, mockGpu, mockModelInfo,
                                                 std::nullopt,   // vllmMonitor
                                                 40.0, 80.0,   // CPU: green=40, red=80
                                                 50.0, 90.0);  // GPU: green=50, red=90
    ASSERT_TRUE(element);
}

TEST_F(SystemResourcesPanelTest, RenderWithEqualTempThresholds)
{
    // Pass equal thresholds (lowC == hiC) directly to render() so the
    // division-by-zero guard (lowC >= hiC -> GreenLight) is exercised.
    ProcessorStats cpuStats;
    cpuStats.usagePercentage = 40.0;
    cpuStats.temperatureC = 50.0;

    MemoryStats vram;
    vram.id = 0;
    vram.totalMb = 24576;
    vram.usedMb = 12288;
    vram.availableMb = 12288;
    vram.usagePercentage = 50.0;

    ProcessorStats gpuLoad;
    gpuLoad.usagePercentage = 50.0;
    gpuLoad.temperatureC = 50.0;

    EXPECT_CALL(mockCpu, getStats()).WillOnce(Return(cpuStats));
    EXPECT_CALL(mockGpu, getStats()).WillOnce(Return(std::vector<MemoryStats>{vram}));
    EXPECT_CALL(mockGpu, getLoadStats())
        .WillOnce(Return(std::vector<ProcessorStats>{gpuLoad}));
    EXPECT_CALL(mockMem, getStats()).Times(1);

    // Inject equal CPU thresholds (50.0, 50.0) — triggers the lowC >= hiC guard
    // in tempColor(). GPU thresholds are distinct. Must not crash.
    auto element = SystemResourcesPanel::render(mockCpu, mockMem, mockGpu, mockModelInfo,
                                                 std::nullopt,   // vllmMonitor
                                                 50.0, 50.0,   // CPU: equal (triggers guard)
                                                 40.0, 90.0);  // GPU: distinct
    ASSERT_TRUE(element);
}
TEST_F(SystemResourcesPanelTest, RenderWithDistinctCpuGpuThresholds)
{
    // CPU temp 65°C with CPU thresholds (30, 80) — mid-range color.
    // GPU temp 55°C with GPU thresholds (50, 90) — different interpolation.
    ProcessorStats cpuStats;
    cpuStats.usagePercentage = 40.0;
    cpuStats.temperatureC = 65.0;

    MemoryStats vram;
    vram.id = 0;
    vram.totalMb = 24576;
    vram.usedMb = 12288;
    vram.availableMb = 12288;
    vram.usagePercentage = 50.0;

    ProcessorStats gpuLoad;
    gpuLoad.usagePercentage = 75.0;
    gpuLoad.temperatureC = 55.0;

    EXPECT_CALL(mockCpu, getStats()).WillOnce(Return(cpuStats));
    EXPECT_CALL(mockGpu, getStats()).WillOnce(Return(std::vector<MemoryStats>{vram}));
    EXPECT_CALL(mockGpu, getLoadStats())
        .WillOnce(Return(std::vector<ProcessorStats>{gpuLoad}));
    EXPECT_CALL(mockMem, getStats()).Times(1);

    // Distinct per-device thresholds — CPU gauge uses CPU thresholds,
    // GPU gauge uses GPU thresholds. Must not crash.
    auto element = SystemResourcesPanel::render(mockCpu, mockMem, mockGpu, mockModelInfo,
                                                 std::nullopt,   // vllmMonitor
                                                 30.0, 80.0,   // CPU: green=30, red=80
                                                 50.0, 90.0);  // GPU: green=50, red=90
    ASSERT_TRUE(element);
}

TEST_F(SystemResourcesPanelTest, RenderWithFlexFillsSpace)
{
    // Verify the panel element is compatible with the | flex modifier used
    // in System Resources Only mode (app.cpp conditional rendering).
    EXPECT_CALL(mockCpu, getStats()).Times(1);
    EXPECT_CALL(mockMem, getStats()).Times(1);
    EXPECT_CALL(mockGpu, getStats()).Times(1);
    EXPECT_CALL(mockGpu, getLoadStats()).Times(1);

    auto element = SystemResourcesPanel::render(mockCpu, mockMem, mockGpu, mockModelInfo);
    ASSERT_TRUE(element);

    // Render with flex in a tall screen — must not crash.
    ftxui::Screen screen(80, 40);
    ftxui::Render(screen, element | ftxui::flex);
    SUCCEED() << "Panel rendered with | flex modifier in tall screen";
}

// =============================================================================
// vLLM Monitor Integration
// =============================================================================

TEST_F(SystemResourcesPanelTest, RenderWithVllmMonitor)
{
    // Render with vllmMonitor present — should not crash.
    ModelInfo vllmInfo;
    vllmInfo.isServerRunning = true;
    vllmInfo.isModelLoaded = true;
    vllmInfo.isIdle = true;
    vllmInfo.loadedModel = "vllm-model";

    EXPECT_CALL(mockCpu, getStats()).Times(1);
    EXPECT_CALL(mockMem, getStats()).Times(1);
    EXPECT_CALL(mockGpu, getStats()).Times(1);
    EXPECT_CALL(mockGpu, getLoadStats()).Times(1);
    EXPECT_CALL(mockVllmMonitor, getStats()).WillOnce(Return(vllmInfo));

    auto element = SystemResourcesPanel::render(mockCpu, mockMem, mockGpu, mockModelInfo,
                                                 std::ref(mockVllmMonitor));
    ASSERT_TRUE(element);
}

TEST_F(SystemResourcesPanelTest, RenderWithVllmMonitorAndTempThresholds)
{
    // Render with both vllmMonitor and custom temp thresholds.
    ProcessorStats cpuStats;
    cpuStats.usagePercentage = 40.0;
    cpuStats.temperatureC = 65.0;

    MemoryStats vram;
    vram.id = 0;
    vram.totalMb = 24576;
    vram.usedMb = 12288;
    vram.availableMb = 12288;
    vram.usagePercentage = 50.0;

    ProcessorStats gpuLoad;
    gpuLoad.usagePercentage = 75.0;
    gpuLoad.temperatureC = 55.0;

    ModelInfo vllmInfo;
    vllmInfo.isServerRunning = true;
    vllmInfo.isModelLoaded = true;
    vllmInfo.isIdle = true;
    vllmInfo.loadedModel = "vllm-model";

    EXPECT_CALL(mockCpu, getStats()).WillOnce(Return(cpuStats));
    EXPECT_CALL(mockGpu, getStats()).WillOnce(Return(std::vector<MemoryStats>{vram}));
    EXPECT_CALL(mockGpu, getLoadStats())
        .WillOnce(Return(std::vector<ProcessorStats>{gpuLoad}));
    EXPECT_CALL(mockMem, getStats()).Times(1);
    EXPECT_CALL(mockVllmMonitor, getStats()).WillOnce(Return(vllmInfo));

    auto element = SystemResourcesPanel::render(mockCpu, mockMem, mockGpu, mockModelInfo,
                                                 std::ref(mockVllmMonitor),
                                                 40.0, 80.0,
                                                 50.0, 90.0);
    ASSERT_TRUE(element);
}

TEST_F(SystemResourcesPanelTest, RenderWithVllmMonitorOffline)
{
    // vLLM server offline — should render without crashing.
    ModelInfo vllmInfo;
    vllmInfo.isServerRunning = false;
    vllmInfo.isModelLoaded = false;

    EXPECT_CALL(mockCpu, getStats()).Times(1);
    EXPECT_CALL(mockMem, getStats()).Times(1);
    EXPECT_CALL(mockGpu, getStats()).Times(1);
    EXPECT_CALL(mockGpu, getLoadStats()).Times(1);
    EXPECT_CALL(mockVllmMonitor, getStats()).WillOnce(Return(vllmInfo));

    auto element = SystemResourcesPanel::render(mockCpu, mockMem, mockGpu, mockModelInfo,
                                                 std::ref(mockVllmMonitor));
    ASSERT_TRUE(element);
}