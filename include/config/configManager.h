#pragma once

#include "IConfigManager.h"
#include "config.h"

#include <optional>
#include <shared_mutex>
#include <string>

/**
 * @file configManager.h
 * @brief Singleton class for loading and saving configuration.
 *
 * ConfigManager is a singleton that handles all configuration persistence
 * using nlohmann/json for serialization. It provides cross-platform support
 * for finding the config directory:
 * - Linux/macOS: ~/.workbench/
 * - Windows: %USERPROFILE%\.workbench\
 *
 * The manager automatically creates the config directory if it doesn't exist,
 * and populates default values if the config file is missing or invalid.
 *
 * Usage:
 * @code
 *   // Load config (typically in main())
 *   ConfigManager::instance().load();
 *   
 *   // Read config from anywhere (thread-safe snapshot)
 *   auto config = ConfigManager::instance().getConfigSnapshot();
 *   std::cout << config.server.host << std::endl;
 *
 *   // Modify and save
 *   config.ui.theme = "dark";
 *   ConfigManager::instance().setConfig(config);
 *   ConfigManager::instance().save();
 * @endcode
 *
 * @note The load() method should be called early in application startup,
 *       before any panels or systems access configuration.
 * @note The save() method can be called at any time to persist changes.
 */
class ConfigManager : public IConfigManager
{
public:
    /**
     * @brief Get the singleton instance.
     * 
     * @return Reference to the single ConfigManager instance.
     * @note Thread-safe; uses Meyers singleton pattern.
     */
    static ConfigManager& instance();

    /**
     * @brief Load configuration from disk.
     * 
* Attempts to read the config file from the standard location
 * (~/.workbench/config.json). If the file doesn't exist or is
     * invalid, populates all fields with default values.
     * 
     * Creates the config directory if it doesn't exist.
     * 
     * @return true if loaded successfully (from file or defaults)
     * @return false if there was an unrecoverable error
     * 
     * @note Should be called once at application startup.
     * @note Subsequent calls will overwrite current config.
     */
    bool load();

    /**
     * @brief Save current configuration to disk.
     * 
     * Serializes the current config to JSON and writes it to
     * the config file. The output is pretty-printed with
     * indentation for human readability.
     * 
     * @return true if saved successfully
     * @return false if there was an error writing the file
     * 
     * @note Creates parent directories if they don't exist.
     * @note Can be called multiple times during application lifetime.
     */
    bool save() override;

    /**
     * @brief Get a copy of the current configuration.
     *
     * @return Snapshot of the current UserConfig, taken under a shared lock.
     * @note Safe to call from any thread.
     */
    [[nodiscard]] Config::UserConfig getConfigSnapshot() const override;

    /**
     * @brief Get a copy of just the server settings.
     *
     * @return Snapshot of ServerSettings, taken under a shared lock.
     * @note Safe to call from any thread; intended for polling threads.
     */
    [[nodiscard]] Config::ServerSettings getServerSettings() const override;

    /**
     * @brief Replace the current configuration.
     *
     * @param cfg New configuration; swapped in under an exclusive lock.
     * @note Changes are not persisted until save() is called.
     */
    void setConfig(const Config::UserConfig &cfg) override;

    /**
     * @brief Check if config has been loaded.
     * 
     * @return true if load() has been called successfully.
     */
    bool isLoaded() const;

    /**
     * @brief Get the config directory path (~/.workbench/).
     * 
     * @return The platform-appropriate config directory path.
     * @note Does not create the directory; use load() for that.
     */
    static std::string getConfigDir();

    /**
     * @brief Get the full config file path.
     * 
     * @return The full path to config.json.
     */
    static std::string getConfigFilePath();

    /**
     * @brief Get the logs directory path (~/.workbench/logs/).
     * 
     * @return The platform-appropriate logs directory path.
     * @note Does not create the directory; it is created when needed.
     */
    static std::string getLogsDir();

    /**
     * @brief Create a default config.json file if one doesn't exist.
     * 
     * Initializes the configuration with all default values and writes
     * them to the config file (~/.workbench/config.json). This method is
     * called automatically by load() when no config file exists, but can
     * also be called explicitly to reset the configuration to defaults.
     * 
     * @return true if the default config was created successfully
     * @return false if there was an error writing the file
     * 
     * @note This method overwrites any current configuration in memory
     *       with default values before saving.
     * @note The config directory is created if it doesn't exist.
     * 
     * @code
     * // Explicitly create default config (e.g., from a "Reset to Defaults" button)
     * ConfigManager::instance().createDefaultConfig();
     * @endcode
     */
    bool createDefaultConfig();

    // ========================================================================
    // TerminalPreset Access Methods
    // ========================================================================

    /**
     * @brief Get a copy of all terminal presets.
     *
     * @return Snapshot of the terminal presets, taken under a shared lock.
     */
    [[nodiscard]] std::vector<Config::TerminalPreset> getTerminalPresets() const override;

    /**
     * @brief Find a terminal preset by name.
     *
     * @param name The name of the preset to find.
     * @return The preset if found, std::nullopt otherwise.
     */
    std::optional<Config::TerminalPreset> findTerminalPreset(const std::string &name) const override;

    /**
     * @brief Add a new terminal preset.
     *
     * @param preset The preset to add.
     * @return true if added successfully, false if a preset with the same name exists.
     */
    bool addTerminalPreset(Config::TerminalPreset preset) override;

    /**
     * @brief Remove a terminal preset by name.
     *
     * @param name The name of the preset to remove.
     * @return true if found and removed, false otherwise.
     */
    bool removeTerminalPreset(const std::string &name) override;

    /**
     * @brief Update an existing terminal preset.
     *
     * @param oldName The name of the preset to update.
     * @param preset The new preset values.
     * @return true if found and updated, false otherwise.
     */
    bool updateTerminalPreset(const std::string &oldName, Config::TerminalPreset preset) override;

private:
    /**
     * @brief Default constructor - private for singleton pattern.
     */
    ConfigManager() = default;
    
    /**
     * @brief Default destructor.
     */
    ~ConfigManager() = default;
    
    /**
     * @brief Delete copy constructor - singleton cannot be copied.
     */
    ConfigManager(const ConfigManager&) = delete;
    
    /**
     * @brief Delete copy assignment - singleton cannot be copied.
     */
    ConfigManager& operator=(const ConfigManager&) = delete;

    /**
     * @brief Delete move constructor - singleton cannot be moved.
     */
    ConfigManager(ConfigManager&&) = delete;
    
    /**
     * @brief Delete move assignment - singleton cannot be moved.
     */
    ConfigManager& operator=(ConfigManager&&) = delete;

    Config::UserConfig config_;
    mutable std::shared_mutex mutex_;
    bool loaded_ = false;
};
