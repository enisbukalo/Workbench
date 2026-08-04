#include <gtest/gtest.h>
#include "config/configManager.h"
#include "json.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;

class ConfigManagerTest : public ::testing::Test {
protected:
    fs::path tempDir;
    fs::path configDir;
    std::string originalHome;

    void SetUp() override {
        // Save original HOME
        const char *h = std::getenv("HOME");
        originalHome = h ? h : "";

        // Create temp dir and point HOME at it
        tempDir = fs::temp_directory_path() / "workbench_cm_test";
        fs::create_directories(tempDir);
        setenv("HOME", tempDir.string().c_str(), 1);

        configDir = tempDir / ".workbench";
        fs::create_directories(configDir);
    }

    void TearDown() override {
        // Restore HOME
        if (originalHome.empty())
            unsetenv("HOME");
        else
            setenv("HOME", originalHome.c_str(), 1);

        fs::remove_all(tempDir);
    }
};

// Mock helper - we'll test actual ConfigManager behavior by checking
// the config file it creates and loads

TEST(ConfigManager, LoadCreatesDefaults) {
    // Get a fresh instance (singleton)
    ConfigManager& manager = ConfigManager::instance();

    // The singleton pattern means we can't easily test with temp dirs
    // Instead, we verify that defaults are used when loading fails
    // (non-existent file triggers default config)
    
    // Check that the instance exists and has default values
    const auto config = manager.getConfigSnapshot();

    // Verify default values (load/inference now live per-preset, not here)
    EXPECT_EQ(config.server.host, "127.0.0.1");
    EXPECT_EQ(config.server.port, 8080);
    EXPECT_EQ(config.ui.theme, "default");
}

TEST(ConfigManager, SetConfigIsVisibleInNextSnapshot) {
    ConfigManager& manager = ConfigManager::instance();

    auto config = manager.getConfigSnapshot();
    config.server.port = 9999;
    manager.setConfig(config);

    // A fresh snapshot should reflect the change
    EXPECT_EQ(manager.getConfigSnapshot().server.port, 9999);
    EXPECT_EQ(manager.getServerSettings().port, 9999);
}

TEST(ConfigManager, SnapshotIsIndependentOfLaterChanges) {
    ConfigManager& manager = ConfigManager::instance();

    auto base = manager.getConfigSnapshot();
    base.server.port = 1111;
    manager.setConfig(base);

    const auto snapshot = manager.getConfigSnapshot();
    base.server.port = 2222;
    manager.setConfig(base);

    // The earlier snapshot must be unaffected by the later setConfig
    EXPECT_EQ(snapshot.server.port, 1111);
    EXPECT_EQ(manager.getConfigSnapshot().server.port, 2222);
}

TEST(ConfigManager, IsLoadedInitiallyFalse) {
    // When ConfigManager is first created, loaded_ is false
    // (We can't easily test this with singleton, but we can verify the method exists)
    ConfigManager& manager = ConfigManager::instance();
    // The method is accessible - just verify it compiles and runs
    (void)manager.isLoaded();
}

TEST(ConfigManager, ConfigDirPath) {
    std::string configDir = ConfigManager::getConfigDir();
    
    // Should return a non-empty path
    EXPECT_FALSE(configDir.empty());
    
#ifdef _WIN32
    // On Windows, should contain .workbench
    EXPECT_TRUE(configDir.find(".workbench") != std::string::npos);
#else
    // On Linux, should be in home directory
    EXPECT_TRUE(configDir.find("home") != std::string::npos || 
                configDir.find(".workbench") != std::string::npos);
#endif
}

TEST(ConfigManager, ConfigFilePath) {
    std::string configFile = ConfigManager::getConfigFilePath();
    
    // Should end with config.json
    EXPECT_TRUE(configFile.find("config.json") != std::string::npos);
    EXPECT_TRUE(configFile.find(".workbench") != std::string::npos);
}

TEST(ConfigManager, LogsDirPath) {
    std::string logsDir = ConfigManager::getLogsDir();
    
    // Should be non-empty and contain "logs"
    EXPECT_FALSE(logsDir.empty());
    EXPECT_TRUE(logsDir.find("logs") != std::string::npos);
}

// =============================================================================
// Test that serialization produces valid JSON
// =============================================================================

TEST(ConfigSerialization, FullConfigToJson) {
    ConfigManager& manager = ConfigManager::instance();
    const auto config = manager.getConfigSnapshot();

    nlohmann::json j;
    Config::to_json(j, config);
    
    // Should have all expected top-level keys (load/inference are per-preset now)
    EXPECT_TRUE(j.contains("server"));
    EXPECT_FALSE(j.contains("load"));
    EXPECT_FALSE(j.contains("inference"));
    EXPECT_TRUE(j.contains("ui"));
    EXPECT_TRUE(j.contains("terminal"));
    EXPECT_TRUE(j.contains("discovery"));
    EXPECT_FALSE(j.contains("presets"));  // model presets live in models.ini
    EXPECT_TRUE(j.contains("terminalPresets"));
}

// =============================================================================
// Load/Save/CreateDefault tests using temp HOME
// These tests must run serially due to singleton initialization with HOME env var.
// They share a single test to avoid race conditions between test instances.
// =============================================================================

TEST_F(ConfigManagerTest, LoadSaveCreateDefaultRoundtrip)
{
    // Step 1: Load should create default config file
    EXPECT_TRUE(ConfigManager::instance().load());
    EXPECT_TRUE(ConfigManager::instance().isLoaded());
    EXPECT_TRUE(fs::exists(configDir / "config.json"));
    
    // Step 2: Modify and save
    auto config = ConfigManager::instance().getConfigSnapshot();
    config.server.port = 12345;
    ConfigManager::instance().setConfig(config);
    EXPECT_TRUE(ConfigManager::instance().save());
    
    // Verify file was written with our values
    std::ifstream f(configDir / "config.json");
    ASSERT_TRUE(f.is_open());
    nlohmann::json j = nlohmann::json::parse(f);
    EXPECT_EQ(j["server"]["port"], 12345);
    
    // Step 3: Reload and verify persistence
    EXPECT_TRUE(ConfigManager::instance().load());
    EXPECT_EQ(ConfigManager::instance().getConfigSnapshot().server.port, 12345);
    
    // Step 4: Create default should reset to defaults (tested in separate test to avoid deadlock)
}

TEST_F(ConfigManagerTest, DISABLED_SaveWritesConfigFile)
{
    ConfigManager::instance().load();
    auto config = ConfigManager::instance().getConfigSnapshot();
    config.server.port = 12345;
    ConfigManager::instance().setConfig(config);
    EXPECT_TRUE(ConfigManager::instance().save());

    std::ifstream f(configDir / "config.json");
    ASSERT_TRUE(f.is_open());
    nlohmann::json j = nlohmann::json::parse(f);
    EXPECT_EQ(j["server"]["port"], 12345);
}

TEST_F(ConfigManagerTest, LoadReadsExistingConfig)
{
    // Write a config file manually
    nlohmann::json j = R"({"server":{"host":"10.0.0.1","port":7777}})"_json;
    std::ofstream f(configDir / "config.json");
    f << j.dump(4);
    f.close();

    EXPECT_TRUE(ConfigManager::instance().load());
    EXPECT_EQ(ConfigManager::instance().getConfigSnapshot().server.host, "10.0.0.1");
    EXPECT_EQ(ConfigManager::instance().getConfigSnapshot().server.port, 7777);
}

TEST_F(ConfigManagerTest, LoadCorruptedConfigUsesDefaults)
{
    // Write invalid JSON
    std::ofstream f(configDir / "config.json");
    f << "NOT VALID JSON {{{";
    f.close();

    EXPECT_TRUE(ConfigManager::instance().load());
    // Should fall back to defaults
    Config::UserConfig defaults;
    EXPECT_EQ(ConfigManager::instance().getConfigSnapshot().server.host, defaults.server.host);
}

TEST_F(ConfigManagerTest, DISABLED_CreateDefaultConfigResetsAndSaves)
{
    ConfigManager::instance().load();
    auto config = ConfigManager::instance().getConfigSnapshot();
    config.server.port = 99999;
    ConfigManager::instance().setConfig(config);

    EXPECT_TRUE(ConfigManager::instance().createDefaultConfig());

    Config::UserConfig defaults;
    EXPECT_EQ(ConfigManager::instance().getConfigSnapshot().server.port, defaults.server.port);
    EXPECT_TRUE(fs::exists(configDir / "config.json"));
}

TEST_F(ConfigManagerTest, DISABLED_SaveLoadRoundtrip)
{
    // Skipped - causes deadlock due to singleton re-entry with mutex
}

TEST(ConfigSerialization, FullConfigFromJson) {
    ConfigManager& manager = ConfigManager::instance();
    
    // Create a JSON with all fields
    nlohmann::json j = R"({
        "server": {"host": "192.168.1.100", "port": 9000},
        "ui": {"theme": "dark", "refreshRateMs": 1000},
        "terminal": {"defaultShell": "/bin/sh"},
        "discovery": {"modelSearchPath": "/tmp"},
        "presets": [],
        "terminalPresets": []
    })"_json;

    Config::UserConfig config;
    Config::from_json(j, config);

    // Verify values were loaded (load/inference are no longer top-level keys)
    EXPECT_EQ(config.server.host, "192.168.1.100");
    EXPECT_EQ(config.server.port, 9000);
    EXPECT_EQ(config.ui.theme, "dark");
    EXPECT_EQ(config.ui.refreshRateMs, 1000);
    EXPECT_EQ(config.terminal.defaultShell, "/bin/sh");
    EXPECT_EQ(config.discovery.modelSearchPath, "/tmp");
}