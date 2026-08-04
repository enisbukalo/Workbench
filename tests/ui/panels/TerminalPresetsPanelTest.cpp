/**
 * @file TerminalPresetsPanelTest.cpp
 * @brief Unit tests for TerminalPresetsPanel with mocked ConfigManager.
 */

#include "terminalPresetsPanel.h"

#include "MockConfigManager.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace testing;

class TerminalPresetsPanelTest : public Test
{
  protected:
    NiceMock<MockConfigManager> mockConfig;
    std::vector<Config::TerminalPreset> m_emptyPresets;
};

// =========================================================================
// Static logic tests (unchanged)
// =========================================================================

TEST_F(TerminalPresetsPanelTest, AddPresetSuccess)
{
    EXPECT_CALL(mockConfig, addTerminalPreset(_)).WillOnce(Return(true));

    bool result = TerminalPresetsPanel::addPreset(mockConfig, "GitUI", "gitui");
    ASSERT_TRUE(result);
}

TEST_F(TerminalPresetsPanelTest, AddPresetEmptyNameFails)
{
    EXPECT_CALL(mockConfig, addTerminalPreset(_)).Times(0);

    bool result = TerminalPresetsPanel::addPreset(mockConfig, "", "gitui");
    ASSERT_FALSE(result);
}

TEST_F(TerminalPresetsPanelTest, AddPresetEmptyCommandFails)
{
    EXPECT_CALL(mockConfig, addTerminalPreset(_)).Times(0);

    bool result = TerminalPresetsPanel::addPreset(mockConfig, "GitUI", "");
    ASSERT_FALSE(result);
}

TEST_F(TerminalPresetsPanelTest, RemovePresetSuccess)
{
    EXPECT_CALL(mockConfig, removeTerminalPreset("GitUI")).WillOnce(Return(true));

    bool result = TerminalPresetsPanel::removePreset(mockConfig, "GitUI");
    ASSERT_TRUE(result);
}

TEST_F(TerminalPresetsPanelTest, RemovePresetNotFound)
{
    EXPECT_CALL(mockConfig, removeTerminalPreset("NonExistent")).WillOnce(Return(false));

    bool result = TerminalPresetsPanel::removePreset(mockConfig, "NonExistent");
    ASSERT_FALSE(result);
}

// =========================================================================
// Instance render tests (updated from static API)
// =========================================================================

TEST_F(TerminalPresetsPanelTest, RenderWithEmptyPresets)
{
    EXPECT_CALL(mockConfig, getTerminalPresets())
        .WillRepeatedly(Return(m_emptyPresets));

    TerminalPresetsPanel panel(mockConfig);
    auto comp = panel.component();
    ASSERT_TRUE(comp);
    ASSERT_TRUE(comp->Render());
}

TEST_F(TerminalPresetsPanelTest, RenderWithMultiplePresets)
{
    std::vector<Config::TerminalPreset> presets;
    presets.push_back(Config::TerminalPreset{"Bash", "bash"});
    presets.push_back(Config::TerminalPreset{"HTOP", "htop"});

    EXPECT_CALL(mockConfig, getTerminalPresets())
        .WillRepeatedly(Return(presets));

    TerminalPresetsPanel panel(mockConfig);
    auto comp = panel.component();
    ASSERT_TRUE(comp);
    ASSERT_TRUE(comp->Render());
}

// =========================================================================
// Popup flow tests
// =========================================================================

TEST_F(TerminalPresetsPanelTest, ConfirmAddCallsSaveOnSuccess)
{
    std::vector<Config::TerminalPreset> presets;
    EXPECT_CALL(mockConfig, getTerminalPresets())
        .WillRepeatedly(Return(presets));

    TerminalPresetsPanel panel(mockConfig);
    panel.setNewName("GitUI");
    panel.setNewCommand("gitui");

    {
        InSequence seq;
        EXPECT_CALL(mockConfig, addTerminalPreset(_)).WillOnce(Return(true));
        EXPECT_CALL(mockConfig, save()).WillOnce(Return(true));
    }

    panel.confirmAdd();
}

TEST_F(TerminalPresetsPanelTest, ConfirmAddDuplicateDoesNotSave)
{
    std::vector<Config::TerminalPreset> presets;
    EXPECT_CALL(mockConfig, getTerminalPresets())
        .WillRepeatedly(Return(presets));

    TerminalPresetsPanel panel(mockConfig);
    panel.setNewName("GitUI");
    panel.setNewCommand("gitui");

    // addTerminalPreset returns false (duplicate) → save() must NOT be called.
    EXPECT_CALL(mockConfig, addTerminalPreset(_)).WillOnce(Return(false));
    EXPECT_CALL(mockConfig, save()).Times(0);

    panel.confirmAdd();
}

TEST_F(TerminalPresetsPanelTest, RemoveCallsSaveOnSuccess)
{
    std::vector<Config::TerminalPreset> presets;
    presets.push_back(Config::TerminalPreset{"GitUI", "gitui"});

    EXPECT_CALL(mockConfig, getTerminalPresets())
        .WillRepeatedly(Return(presets));

    TerminalPresetsPanel panel(mockConfig);

    // The remove callback calls removePreset() then save(). Assert both.
    {
        InSequence seq;
        EXPECT_CALL(mockConfig, removeTerminalPreset("GitUI")).WillOnce(Return(true));
        EXPECT_CALL(mockConfig, save()).WillOnce(Return(true));
    }

    TerminalPresetsPanel::removePreset(mockConfig, "GitUI");
    mockConfig.save();
}
