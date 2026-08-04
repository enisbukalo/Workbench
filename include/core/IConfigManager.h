#pragma once

#include "config.h"

#include <optional>
#include <string>
#include <vector>

/**
 * @file IConfigManager.h
 * @brief Thin interface capturing ConfigManager methods used by panels.
 *
 * Panels depend on this interface rather than the singleton directly,
 * enabling unit testing with GMock. The real ConfigManager implements
 * this interface directly (zero indirection overhead).
 */
class IConfigManager
{
  public:
    virtual ~IConfigManager() = default;

    /**
     * @brief Get a copy of the current configuration.
     *
     * Returns a snapshot taken under a shared lock. Safe to call from any
     * thread; the returned value is independent of later modifications.
     */
    [[nodiscard]] virtual Config::UserConfig getConfigSnapshot() const = 0;

    /**
     * @brief Get a copy of just the server settings.
     *
     * Cheap snapshot for the hot polling paths (health checks, slot/model
     * queries) that only need host/port. Safe to call from any thread.
     */
    [[nodiscard]] virtual Config::ServerSettings getServerSettings() const = 0;

    /**
     * @brief Replace the current configuration.
     *
     * Swaps the whole config under an exclusive lock. Does not persist;
     * call save() afterwards. Safe to call from any thread.
     */
    virtual void setConfig(const Config::UserConfig &cfg) = 0;

    /** @brief Get a copy of all terminal presets (taken under shared lock). */
    [[nodiscard]] virtual std::vector<Config::TerminalPreset> getTerminalPresets() const = 0;

    /** @brief Find a terminal preset by name. */
    virtual std::optional<Config::TerminalPreset> findTerminalPreset(const std::string &name) const = 0;

    /** @brief Add a new terminal preset. @return true if added. */
    virtual bool addTerminalPreset(Config::TerminalPreset preset) = 0;

    /** @brief Remove a terminal preset by name. @return true if removed. */
    virtual bool removeTerminalPreset(const std::string &name) = 0;

    /** @brief Update an existing terminal preset. @return true if updated. */
    virtual bool updateTerminalPreset(const std::string &oldName, Config::TerminalPreset preset) = 0;

    /** @brief Save current configuration to disk. @return true on success. */
    virtual bool save() = 0;
};
