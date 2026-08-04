/**
 * @file statusBarSettings.cpp
 * @brief Serialization, validation, and standalone loader for StatusBarSettings.
 *
 * Kept separate from config.cpp because the status bar file
 * (~/.workbench/statusbar.json) is independent of config.json and does not
 * flow through ConfigManager. The test build globs the src/config directory,
 * so this TU is picked up automatically.
 */

#include "statusBarSettings.h"

#include "configManager.h"

#include <algorithm>
#include <fstream>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace Config {

void StatusBarSettings::validate() noexcept
{
	hintIntervalMs = std::max(hintIntervalMs, 1);
}

void to_json(json &j, const StatusBarSettings &v)
{
	j = json{ { "hintIntervalMs", v.hintIntervalMs },
			  { "hints", v.hints },
			  { "keymaps", v.keymaps } };
}

void from_json(const json &j, StatusBarSettings &v)
{
	v.hintIntervalMs = j.value("hintIntervalMs", v.hintIntervalMs);
	v.hints = j.value("hints", v.hints);
	v.keymaps = j.value("keymaps", v.keymaps);
	v.validate();
}

std::string statusBarSettingsPath()
{
	return ConfigManager::getConfigDir() + "/statusbar.json";
}

namespace {
/**
 * @brief Build the default, self-documenting status-bar settings.
 *
 * Seeds a few example hints plus a keymap entry per known built-in tab so
 * the file is self-explanatory on first write.
 */
StatusBarSettings makeDefaults()
{
	StatusBarSettings s;
	s.hints = {
		"Tip: press Tab to switch panels",
		"Edit ~/.workbench/statusbar.json to change these hints",
		"Ctrl+C in a terminal tab sends SIGINT to the shell",
	};
	s.keymaps = {
		{ "App Settings", { "Enter: edit", "S: save" } },
		{ "Model Settings", { "Enter: select model", "L: load" } },
		{ "Server Log", { "read-only" } },
		{ "Terminal", { "type to interact", "Ctrl+C: interrupt" } },
	};
	s.validate();
	return s;
}
} // namespace

StatusBarSettings loadStatusBarSettings(const std::string &path)
{
	std::ifstream file(path);
	if (!file.is_open()) {
		spdlog::info("Status-bar file not found at {}, creating with defaults",
					 path);
		StatusBarSettings defaults = makeDefaults();
		// Best-effort write so the user has something to edit. Failure
		// (read-only dir, permissions) is logged and ignored; defaults are
		// still returned.
		try {
			std::ofstream out(path);
			if (out.is_open()) {
				json j = defaults;
				out << j.dump(4);
			} else {
				spdlog::warn("Failed to open status-bar file for writing: {}",
							 path);
			}
		} catch (const std::exception &e) {
			spdlog::warn("Failed to write default status-bar file {}: {}",
						 path,
						 e.what());
		}
		return defaults;
	}

	try {
		json j;
		file >> j;
		auto settings = j.get<StatusBarSettings>();
		spdlog::info("Status-bar settings loaded from {}", path);
		return settings;
	} catch (const std::exception &e) {
		spdlog::warn("Failed to parse status-bar file {}: {} — using defaults",
					 path,
					 e.what());
		return makeDefaults();
	}
}

} // namespace Config
