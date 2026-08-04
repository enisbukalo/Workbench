#pragma once

#include <functional> // std::less<> transparent comparator
#include <map>
#include <string>
#include <vector>

#include "json.hpp"

/**
 * @file statusBarSettings.h
 * @brief Status-bar configuration: rolling hints + per-tab keymap hints.
 *
 * Backs the bottom status bar of the app. Stored in a standalone file
 * (~/.workbench/statusbar.json) independent of config.json, so these
 * decls live here rather than in config.h — StatusBarSettings is NOT a
 * member of UserConfig.
 *
 * Loaded once at startup (no hot reload): edit the file then restart.
 */

namespace Config {

/**
 * @brief Data backing the bottom status bar.
 *
 * - @c hints are cycled in the center of the bar over time.
 * - @c hintIntervalMs controls how long each hint stays visible.
 * - @c keymaps maps an active tab name to keymap hint strings shown on the
 *   right of the bar when that tab is active.
 */
struct StatusBarSettings
{
	/**
	 * @brief Rolling hints cycled in the center of the bar.
	 *
	 * Empty => no center hint rendered.
	 */
	std::vector<std::string> hints;

	/**
	 * @brief How long each hint stays visible before advancing, in ms.
	 *
	 * Time-based (not frame-based) so rotation timing is stable regardless
	 * of how many times FTXUI re-renders per visual frame. Clamped >= 1 by
	 * validate() to avoid divide-by-zero in the rotation math.
	 *
	 * @default 5000 (5 seconds)
	 */
	int hintIntervalMs = 5000;

	/**
	 * @brief Per-tab keymap hints, keyed by tab name.
	 *
	 * Keys match app.cpp tabValues ("App Settings", "Model Settings",
	 * "Server Log", "Terminal", plus terminal-preset names). Shown only when
	 * that tab is active; a missing key means no keymap hint. Keyed by name,
	 * not index, because dynamic preset tabs shift indices.
	 *
	 * Uses std::less<> transparent comparator to enable heterogeneous lookup
	 * with std::string_view — no temporary std::string per render call.
	 */
	using KeymapMap =
		std::map<std::string, std::vector<std::string>, std::less<>>;
	KeymapMap keymaps;

	/**
	 * @brief Clamp numeric fields to valid ranges in-place.
	 *
	 * Clamps hintIntervalMs >= 1. Called at the end of from_json.
	 */
	void validate() noexcept;
};

/**
 * @brief Serialize StatusBarSettings to JSON.
 *
 * Kept here (not config.h) because StatusBarSettings is independent of
 * UserConfig. Same namespace as the struct so nlohmann ADL finds it.
 */
void to_json(nlohmann::json &j, const StatusBarSettings &v);

/**
 * @brief Deserialize StatusBarSettings from JSON.
 *
 * Uses j.value(key, default) graceful fallback for missing keys, then
 * calls validate(). Mirrors the UISettings::from_json style.
 */
void from_json(const nlohmann::json &j, StatusBarSettings &v);

/**
 * @brief Full path to the status-bar settings file.
 *
 * @return ConfigManager::getConfigDir() + "/statusbar.json".
 * @note getConfigDir() is static and callable without a full config load.
 */
std::string statusBarSettingsPath();

/**
 * @brief Load status-bar settings from a JSON file.
 *
 * On a missing file: returns a default-populated StatusBarSettings AND
 * best-effort writes the defaults to @p path so the user has something to
 * edit. On invalid/malformed JSON: logs via spdlog::warn and returns
 * defaults. Never throws — write failures (read-only dir, permissions) are
 * logged and ignored; defaults are still returned.
 *
 * @param path Path to the status-bar JSON file.
 * @return Parsed settings, or defaults on any failure.
 */
StatusBarSettings loadStatusBarSettings(const std::string &path);

} // namespace Config
