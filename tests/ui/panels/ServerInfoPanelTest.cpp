/**
 * @file ServerInfoPanelTest.cpp
 * @brief Unit tests for ServerInfoPanel::render() with mocked server.
 */

#include "serverInfoPanel.h"

#include "MockLlamaServerProcess.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace testing;

class ServerInfoPanelTest : public Test
{
  protected:
    NiceMock<MockLlamaServerProcess> mockServer;
};

TEST_F(ServerInfoPanelTest, RenderWithRunningServer)
{
    EXPECT_CALL(mockServer, isRunning()).WillOnce(Return(true));

    auto element = ServerInfoPanel::render(mockServer);
    ASSERT_TRUE(element);
}

TEST_F(ServerInfoPanelTest, RenderWithStoppedServer)
{
    EXPECT_CALL(mockServer, isRunning()).WillOnce(Return(false));

    auto element = ServerInfoPanel::render(mockServer);
    ASSERT_TRUE(element);
}
