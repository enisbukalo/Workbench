/**
 * @file ModelInfoPanelTest.cpp
 * @brief Unit tests for ModelInfoPanel::render() with mocked monitor.
 */

#include "modelInfoPanel.h"

#include "MockModelInfoMonitor.h"
#include "MockVllmMonitor.h"

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <map>
#include <string>

using namespace testing;
using namespace ftxui;

namespace {
// Render an element to a wide screen and return its flattened text, so tests
// can assert on visible content (model names) rather than DOM structure.
std::string renderToText(const Element &element)
{
    // Clone: Render() takes a non-const Element&; copy keeps the caller's const.
    Element root = element;
    Screen screen(120, 10);
    Render(screen, root);
    return screen.ToString();
}

ModelInfo makeLoaded(const std::string &name)
{
    ModelInfo info;
    info.isServerRunning = true;
    info.isModelLoaded = true;
    info.isIdle = true;
    info.loadedModel = name;
    info.generationTokensPerSec = 10.0;
    info.processingTokensPerSec = 20.0;
    return info;
}
} // namespace

class ModelInfoPanelTest : public Test
{
  protected:
    NiceMock<MockModelInfoMonitor> mockMonitor;
    NiceMock<MockVllmMonitor> mockVllmMonitor;
};

TEST_F(ModelInfoPanelTest, RenderWithNoServer)
{
    ModelInfo info;
    info.isServerRunning = false;
    info.isModelLoaded = false;

    EXPECT_CALL(mockMonitor, getStats()).WillOnce(Return(info));

    auto element = ModelInfoPanel::render(mockMonitor);
    ASSERT_TRUE(element);
}

TEST_F(ModelInfoPanelTest, RenderWithIdleModel)
{
    ModelInfo info;
    info.isServerRunning = true;
    info.isModelLoaded = true;
    info.isIdle = true;
    info.loadedModel = "test-model";
    info.generationTokensPerSec = 45.2;
    info.processingTokensPerSec = 120.5;

    EXPECT_CALL(mockMonitor, getStats()).WillOnce(Return(info));

    auto element = ModelInfoPanel::render(mockMonitor);
    ASSERT_TRUE(element);
}

TEST_F(ModelInfoPanelTest, RenderWithProcessingModel)
{
    ModelInfo info;
    info.isServerRunning = true;
    info.isModelLoaded = true;
    info.isIdle = false;
    info.activeRequestCount = 2;
    info.loadedModel = "processing-model";
    info.generationTokensPerSec = 30.0;
    info.processingTokensPerSec = 50.0;

    EXPECT_CALL(mockMonitor, getStats()).WillOnce(Return(info));

    auto element = ModelInfoPanel::render(mockMonitor);
    ASSERT_TRUE(element);
}

TEST_F(ModelInfoPanelTest, RenderWithNoModelLoaded)
{
    ModelInfo info;
    info.isServerRunning = true;
    info.isModelLoaded = false;

    EXPECT_CALL(mockMonitor, getStats()).WillOnce(Return(info));

    auto element = ModelInfoPanel::render(mockMonitor);
    ASSERT_TRUE(element);
}

// =============================================================================
// Multi-model: one column per loaded model (#110)
// =============================================================================

TEST_F(ModelInfoPanelTest, Render_TwoModels_ShowsBothNames)
{
    std::map<std::string, ModelInfo> models{
        {"alpha", makeLoaded("alpha")},
        {"beta", makeLoaded("beta")},
    };
    EXPECT_CALL(mockMonitor, getAllStats()).WillOnce(Return(models));

    auto element = ModelInfoPanel::render(mockMonitor);
    ASSERT_TRUE(element);

    const std::string out = renderToText(element);
    EXPECT_THAT(out, HasSubstr("alpha"));
    EXPECT_THAT(out, HasSubstr("beta"));
}

TEST_F(ModelInfoPanelTest, Render_NoModels_ShowsNone)
{
    // Empty map -> fall back to the aggregate single column. Server up, none.
    ModelInfo agg;
    agg.isServerRunning = true;
    agg.isModelLoaded = false;
    EXPECT_CALL(mockMonitor, getAllStats())
        .WillOnce(Return(std::map<std::string, ModelInfo>{}));
    EXPECT_CALL(mockMonitor, getStats()).WillOnce(Return(agg));

    const std::string out = renderToText(ModelInfoPanel::render(mockMonitor));
    EXPECT_THAT(out, HasSubstr("None"));
}

TEST_F(ModelInfoPanelTest, Render_ServerOffline_ShowsOffline)
{
    ModelInfo agg;
    agg.isServerRunning = false;
    agg.isModelLoaded = false;
    EXPECT_CALL(mockMonitor, getAllStats())
        .WillOnce(Return(std::map<std::string, ModelInfo>{}));
    EXPECT_CALL(mockMonitor, getStats()).WillOnce(Return(agg));

    const std::string out = renderToText(ModelInfoPanel::render(mockMonitor));
    EXPECT_THAT(out, HasSubstr("Offline"));
}

// =============================================================================
// Dual-column: LLAMA + VLLM
// =============================================================================

TEST_F(ModelInfoPanelTest, Render_WithVllmMonitor_BothColumnsRender)
{
    // LLAMA: server running, model loaded
    ModelInfo llamaInfo;
    llamaInfo.isServerRunning = true;
    llamaInfo.isModelLoaded = true;
    llamaInfo.isIdle = true;
    llamaInfo.loadedModel = "llama-model";

    // VLLM: server running, model loaded
    ModelInfo vllmInfo;
    vllmInfo.isServerRunning = true;
    vllmInfo.isModelLoaded = true;
    vllmInfo.isIdle = true;
    vllmInfo.loadedModel = "vllm-model";

    EXPECT_CALL(mockMonitor, getAllStats())
        .WillOnce(Return(std::map<std::string, ModelInfo>{}));
    EXPECT_CALL(mockMonitor, getStats()).WillOnce(Return(llamaInfo));
    EXPECT_CALL(mockVllmMonitor, getStats()).WillOnce(Return(vllmInfo));

    auto element = ModelInfoPanel::render(mockMonitor, std::ref(mockVllmMonitor));
    ASSERT_TRUE(element);

    const std::string out = renderToText(element);
    EXPECT_THAT(out, HasSubstr("LLAMA"));
    EXPECT_THAT(out, HasSubstr("VLLM"));
    EXPECT_THAT(out, HasSubstr("llama-model"));
    EXPECT_THAT(out, HasSubstr("vllm-model"));
}

TEST_F(ModelInfoPanelTest, Render_WithVllmMonitor_VllmOffline)
{
    // LLAMA: server running
    ModelInfo llamaInfo;
    llamaInfo.isServerRunning = true;
    llamaInfo.isModelLoaded = true;
    llamaInfo.isIdle = true;
    llamaInfo.loadedModel = "llama-model";

    // VLLM: server offline
    ModelInfo vllmInfo;
    vllmInfo.isServerRunning = false;
    vllmInfo.isModelLoaded = false;

    EXPECT_CALL(mockMonitor, getAllStats())
        .WillOnce(Return(std::map<std::string, ModelInfo>{}));
    EXPECT_CALL(mockMonitor, getStats()).WillOnce(Return(llamaInfo));
    EXPECT_CALL(mockVllmMonitor, getStats()).WillOnce(Return(vllmInfo));

    auto element = ModelInfoPanel::render(mockMonitor, std::ref(mockVllmMonitor));
    ASSERT_TRUE(element);

    const std::string out = renderToText(element);
    EXPECT_THAT(out, HasSubstr("LLAMA"));
    EXPECT_THAT(out, HasSubstr("VLLM"));
    EXPECT_THAT(out, HasSubstr("Offline"));
}

TEST_F(ModelInfoPanelTest, Render_WithVllmMonitor_NoLlamaServer)
{
    // LLAMA: server offline
    ModelInfo llamaInfo;
    llamaInfo.isServerRunning = false;
    llamaInfo.isModelLoaded = false;

    // VLLM: server running
    ModelInfo vllmInfo;
    vllmInfo.isServerRunning = true;
    vllmInfo.isModelLoaded = true;
    vllmInfo.isIdle = true;
    vllmInfo.loadedModel = "vllm-model";

    EXPECT_CALL(mockMonitor, getAllStats())
        .WillOnce(Return(std::map<std::string, ModelInfo>{}));
    EXPECT_CALL(mockMonitor, getStats()).WillOnce(Return(llamaInfo));
    EXPECT_CALL(mockVllmMonitor, getStats()).WillOnce(Return(vllmInfo));

    auto element = ModelInfoPanel::render(mockMonitor, std::ref(mockVllmMonitor));
    ASSERT_TRUE(element);

    const std::string out = renderToText(element);
    EXPECT_THAT(out, HasSubstr("LLAMA"));
    EXPECT_THAT(out, HasSubstr("VLLM"));
    EXPECT_THAT(out, HasSubstr("Offline"));
}

TEST_F(ModelInfoPanelTest, Render_WithoutVllmMonitor_NoServerHeaders)
{
    // Without vllmMonitor, no "LLAMA" header should appear
    ModelInfo info;
    info.isServerRunning = true;
    info.isModelLoaded = true;
    info.isIdle = true;
    info.loadedModel = "test-model";

    EXPECT_CALL(mockMonitor, getAllStats())
        .WillOnce(Return(std::map<std::string, ModelInfo>{}));
    EXPECT_CALL(mockMonitor, getStats()).WillOnce(Return(info));

    auto element = ModelInfoPanel::render(mockMonitor);
    ASSERT_TRUE(element);

    const std::string out = renderToText(element);
    // Should not contain "LLAMA" header when vllmMonitor is absent
    EXPECT_THAT(out, Not(HasSubstr("LLAMA")));
    EXPECT_THAT(out, HasSubstr("test-model"));
}