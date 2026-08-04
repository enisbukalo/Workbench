/**
 * @file configManager.cpp
 * @brief Implementation of the ConfigManager singleton.
 *
 * This file implements the ConfigManager class, which provides a
 * thread-safe singleton for loading and saving application configuration.
 *
 * The configuration is stored as JSON at:
 * - Linux/macOS: ~/.workbench/config.json
 * - Windows: %USERPROFILE%\.workbench\config.json
 *
 * The load() method uses graceful degradation:
 * 1. Creates config directory if it doesn't exist
 * 2. Attempts to parse existing config file
 * 3. Falls back to default values if file is missing or invalid
 * 4. Saves default config if no file existed
 *
 * The save() method serializes the current config to pretty-printed JSON
 * with 4-space indentation for human readability.
 *
 * @note Uses Meyers singleton pattern for thread-safe, lazy initialization.
 * @note The singleton is destroyed at program termination, but config
 *       should be saved explicitly before exit if modifications were made.
 *
 * @see configManager.h for API documentation
 * @see config.h for UserConfig structure documentation
 */

#include "configManager.h"
#include "eventBus.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>

using json = nlohmann::json;
namespace fs = std::filesystem;

ConfigManager &ConfigManager::instance()
{
	static ConfigManager instance;
	return instance;
}

std::string ConfigManager::getConfigDir()
{
#ifdef _WIN32
	const char *home = std::getenv("USERPROFILE");
#else
	const char *home = std::getenv("HOME");
#endif
	if (!home)
		return ".workbench";
#ifdef _WIN32
	return std::string(home) + "\\.workbench";
#else
	return std::string(home) + "/.workbench";
#endif
}

std::string ConfigManager::getConfigFilePath()
{
	return getConfigDir() + "/config.json";
}

std::string ConfigManager::getLogsDir()
{
	return getConfigDir() + "/logs";
}

bool ConfigManager::load()
{
	std::unique_lock lock(mutex_);

	std::string configDir = getConfigDir();
	std::string configFile = getConfigFilePath();

	/*
	 * Load algorithm:
	 * 1. Create config directory if it doesn't exist
	 * 2. Try to open and parse existing config file
	 * 3. If file exists but is invalid JSON, fall back to defaults
	 * 4. If file doesn't exist, use defaults and create config file
	 *
	 * This ensures the application always has valid configuration,
	 * even if the config file is corrupted or missing.
	 */

	// Create config directory if it doesn't exist
	try {
		fs::create_directories(configDir);
	} catch (const std::exception &e) {
		// If we can't create the directory, use defaults
		spdlog::warn("Failed to create config dir '{}', using defaults: {}",
					 configDir,
					 e.what());
		config_ = Config::UserConfig{};
		loaded_ = true;
		return true;
	}

	// Check if config file exists
	bool fileExists = fs::exists(configFile);

	// Try to read existing config file
	std::ifstream file(configFile);
	if (file.is_open()) {
		try {
			json j = json::parse(file);
			config_ = j.get<Config::UserConfig>();
			spdlog::info("Config loaded from '{}'", configFile);
		} catch (const std::exception &e) {
			// Invalid JSON, use defaults
			spdlog::warn("Config file '{}' corrupted, using defaults: {}",
						 configFile,
						 e.what());
			config_ = Config::UserConfig{};
		}
	} else {
		// No config file, use defaults and create it
		if (!fileExists) {
			// Initialize with default values
			config_ = Config::UserConfig{};
			// Create default config file for the user
			// NOTE: Must release lock first - createDefaultConfig() calls save()
			// which also needs the lock
			lock.unlock();
			createDefaultConfig();
			lock.lock();
			spdlog::info("Created default config at '{}'", configFile);
		} else {
			// File exists but couldn't be opened, use defaults
			spdlog::warn("Could not open config file '{}', using defaults",
						 configFile);
			config_ = Config::UserConfig{};
		}
	}

	loaded_ = true;
	return true;
}

bool ConfigManager::save()
{
	std::string configDir = getConfigDir();
	std::string configFile = getConfigFilePath();

	try {
		fs::create_directories(configDir);
	} catch (const std::exception &e) {
		spdlog::error(
			"Failed to save config: could not create directory '{}': {}",
			configDir,
			e.what());
		return false;
	}

	// Serialize a snapshot taken under the lock; write the file outside it so
	// slow disk I/O never blocks readers.
	json j;
	{
		std::shared_lock lock(mutex_);
		j = config_;
	}

	std::ofstream file(configFile);
	if (!file.is_open()) {
		spdlog::error("Failed to save config: could not open file '{}'",
					  configFile);
		return false;
	}
	file << j.dump(4) << std::endl;

	spdlog::info("Config saved to '{}'", configFile);

	// Publish config.saved event — subscribers re-read via the manager's
	// snapshot accessors; no pointer into internal state is handed out.
	EventBus::publish("config.saved", nullptr);

	return true;
}

Config::UserConfig ConfigManager::getConfigSnapshot() const
{
	std::shared_lock lock(mutex_);
	return config_;
}

Config::ServerSettings ConfigManager::getServerSettings() const
{
	std::shared_lock lock(mutex_);
	return config_.server;
}

void ConfigManager::setConfig(const Config::UserConfig &cfg)
{
	std::unique_lock lock(mutex_);
	config_ = cfg;
}

bool ConfigManager::isLoaded() const
{
	return loaded_;
}

bool ConfigManager::createDefaultConfig()
{
	// This method creates a default config.json file with all default values
	// It is called automatically when no config file exists during load()
	// It can also be called explicitly to reset the config to defaults

	// Ensure config_ has default values
	{
		std::unique_lock lock(mutex_);
		config_ = Config::UserConfig{};
	}

	spdlog::info("Created default config");

	// save() will handle creating the directory and writing the file
	return save();
}

// ============================================================================
// TerminalPreset Access Methods
// ============================================================================

std::vector<Config::TerminalPreset> ConfigManager::getTerminalPresets() const
{
	std::shared_lock lock(mutex_);
	return config_.terminalPresets;
}

std::optional<Config::TerminalPreset>
ConfigManager::findTerminalPreset(const std::string &name) const
{
	std::shared_lock lock(mutex_);
	for (const auto &preset : config_.terminalPresets) {
		if (preset.name == name)
			return preset;
	}
	return std::nullopt;
}

bool ConfigManager::addTerminalPreset(Config::TerminalPreset preset)
{
	std::unique_lock lock(mutex_);
	// Check for duplicate name
	for (const auto &existing : config_.terminalPresets) {
		if (existing.name == preset.name)
			return false;
	}
	config_.terminalPresets.push_back(std::move(preset));
	return true;
}

bool ConfigManager::removeTerminalPreset(const std::string &name)
{
	std::unique_lock lock(mutex_);
	auto it = std::find_if(
		config_.terminalPresets.begin(),
		config_.terminalPresets.end(),
		[&](const Config::TerminalPreset &p) { return p.name == name; });
	if (it != config_.terminalPresets.end()) {
		config_.terminalPresets.erase(it);
		return true;
	}
	return false;
}

bool ConfigManager::updateTerminalPreset(const std::string &oldName,
										 Config::TerminalPreset preset)
{
	std::unique_lock lock(mutex_);
	auto it = std::find_if(
		config_.terminalPresets.begin(),
		config_.terminalPresets.end(),
		[&](const Config::TerminalPreset &p) { return p.name == oldName; });
	if (it != config_.terminalPresets.end()) {
		*it = std::move(preset);
		return true;
	}
	return false;
}
