#include "configManager.h"
#include <gtest/gtest.h>

/**
 * @brief Tests for ConfigManager terminal preset CRUD methods.
 *
 * These tests exercise the in-memory preset management methods
 * (add, remove, find, update) on the singleton instance.
 */
class ConfigManagerPresetsTest : public ::testing::Test
{
  protected:
	void SetUp() override
	{
		// Clear any presets from previous tests
		auto cfg = ConfigManager::instance().getConfigSnapshot();
		cfg.terminalPresets.clear();
		ConfigManager::instance().setConfig(cfg);
	}
};

TEST_F(ConfigManagerPresetsTest, AddTerminalPreset_Success)
{
	Config::TerminalPreset preset;
	preset.name = "test-preset";
	preset.initialCommand = "/bin/bash";
	EXPECT_TRUE(ConfigManager::instance().addTerminalPreset(preset));

	const auto presets = ConfigManager::instance().getTerminalPresets();
	EXPECT_EQ(presets.size(), 1u);
	EXPECT_EQ(presets[0].name, "test-preset");
}

TEST_F(ConfigManagerPresetsTest, AddTerminalPreset_DuplicateFails)
{
	Config::TerminalPreset preset;
	preset.name = "dup";
	EXPECT_TRUE(ConfigManager::instance().addTerminalPreset(preset));
	EXPECT_FALSE(ConfigManager::instance().addTerminalPreset(preset));
	EXPECT_EQ(ConfigManager::instance().getTerminalPresets().size(), 1u);
}

TEST_F(ConfigManagerPresetsTest, RemoveTerminalPreset_Success)
{
	Config::TerminalPreset preset;
	preset.name = "to-remove";
	ConfigManager::instance().addTerminalPreset(preset);
	EXPECT_TRUE(ConfigManager::instance().removeTerminalPreset("to-remove"));
	EXPECT_TRUE(ConfigManager::instance().getTerminalPresets().empty());
}

TEST_F(ConfigManagerPresetsTest, RemoveTerminalPreset_NotFound)
{
	EXPECT_FALSE(ConfigManager::instance().removeTerminalPreset("nonexistent"));
}

TEST_F(ConfigManagerPresetsTest, FindTerminalPreset_Found)
{
	Config::TerminalPreset preset;
	preset.name = "findme";
	preset.initialCommand = "/bin/zsh";
	ConfigManager::instance().addTerminalPreset(preset);

	auto found = ConfigManager::instance().findTerminalPreset("findme");
	ASSERT_TRUE(found.has_value());
	EXPECT_EQ(found->name, "findme");
	EXPECT_EQ(found->initialCommand, "/bin/zsh");
}

TEST_F(ConfigManagerPresetsTest, FindTerminalPreset_NotFound)
{
	auto found = ConfigManager::instance().findTerminalPreset("missing");
	EXPECT_FALSE(found.has_value());
}

TEST_F(ConfigManagerPresetsTest, UpdateTerminalPreset_Success)
{
	Config::TerminalPreset preset;
	preset.name = "original";
	preset.initialCommand = "/bin/bash";
	ConfigManager::instance().addTerminalPreset(preset);

	Config::TerminalPreset updated;
	updated.name = "updated";
	updated.initialCommand = "/bin/zsh";
	EXPECT_TRUE(
		ConfigManager::instance().updateTerminalPreset("original", updated));

	auto found = ConfigManager::instance().findTerminalPreset("updated");
	ASSERT_TRUE(found.has_value());
	EXPECT_EQ(found->initialCommand, "/bin/zsh");
	// Original name should be gone
	EXPECT_FALSE(
		ConfigManager::instance().findTerminalPreset("original").has_value());
}

TEST_F(ConfigManagerPresetsTest, UpdateTerminalPreset_NotFound)
{
	Config::TerminalPreset updated;
	updated.name = "new";
	EXPECT_FALSE(
		ConfigManager::instance().updateTerminalPreset("nonexistent", updated));
}

TEST_F(ConfigManagerPresetsTest, GetTerminalPresets_Empty)
{
	EXPECT_TRUE(ConfigManager::instance().getTerminalPresets().empty());
}

TEST_F(ConfigManagerPresetsTest, GetTerminalPresets_Multiple)
{
	for (int i = 0; i < 3; ++i) {
		Config::TerminalPreset p;
		p.name = "preset-" + std::to_string(i);
		ConfigManager::instance().addTerminalPreset(p);
	}
	EXPECT_EQ(ConfigManager::instance().getTerminalPresets().size(), 3u);
}
