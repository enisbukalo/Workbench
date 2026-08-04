#pragma once

#include <ftxui/dom/linear_gradient.hpp>
#include <ftxui/screen/color.hpp>

#include "eventBus.h"
#include "themeSettings.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @file ThemeManager.h
 * @brief Singleton that manages theme loading, resolution, and switching.
 *
 * Loads themes from bundled and user directories at startup, resolves hex
 * color strings into ftxui::Color values, and provides thread-safe access
 * to the currently active resolved theme for UI panels.
 *
 * Subscribes to "config.ui.theme" EventBus events to react when the user
 * changes theme via settings.  The next FTXUI render cycle picks up new colors.
 */
class ThemeManager {
public:
	/**
	 * @brief Pre-resolved theme with ftxui::Color values.
	 *
	 * Panels read from this struct — not raw hex strings — to avoid
	 * per-frame parsing overhead.  All fields are populated by resolveAndApply().
	 */
	struct ResolvedTheme {
		ftxui::Color success, warning, error, info;
		ftxui::Color label, title, text, mutedText, separator;
		ftxui::Color toggleOn, toggleOff, focusAccent;
		ftxui::Color gaugeBg, tableHeaderBg, tableHeaderText;
		ftxui::Color tableAltEven, tableAltOdd;
		ftxui::Color capturingBorder, activeTabBg, selectionBg, subtleLabel, deleteAction;
		ftxui::LinearGradient cpuLoadGauge;
		ftxui::LinearGradient memoryUsageGauge;
	};

	/** @brief Meyers' singleton — thread-safe lazy initialization. */
	static ThemeManager &instance();

	/**
	 * @brief Load all themes from the user theme directory.
	 *
	 * Must be called once during application startup before any panel reads
	 * the active theme.  Ensures @p userPath exists, (re)writes default.json
	 * from the internal default palette, then scans the directory for *.json
	 * files, validates each, and activates the theme matching cfg.ui.theme
	 * (or "default").
	 *
	 * @param userPath Theme directory next to the config (~/.workbench/themes/)
	 */
	void initialize(const std::string &userPath);

	/**
	 * @brief Get currently active resolved theme — lock-free, thread-safe.
	 *
	 * Returns a shared_ptr snapshot of the active theme.  setActive() builds a
	 * new ResolvedTheme off to the side and atomically swaps the pointer, so a
	 * reader holding this shared_ptr keeps a valid theme for its whole render
	 * cycle even if a theme switch happens mid-frame.  No torn reads.
	 *
	 * Hold the returned pointer for the duration of a render; do not cache the
	 * dereferenced reference across frames.
	 */
	[[nodiscard]] std::shared_ptr<const ResolvedTheme> getActive() const noexcept;

	/**
	 * @brief Switch active theme by name.
	 *
	 * @param name Theme name (e.g. "Default", "Dark")
	 * @return true if theme found and activated, false otherwise
	 */
	bool setActive(const std::string &name);

	/**
	 * @brief Get available theme names for UI dropdown.
	 *
	 * Returns copy of stable-ordered list of display names.
	 */
	[[nodiscard]] std::vector<std::string> getAvailableThemes() const noexcept;

	/**
	 * @brief Get raw Theme struct by name (for editing, preview).
	 *
	 * @param name Theme display name
	 * @return Theme if found, nullopt otherwise
	 */
	[[nodiscard]] std::optional<Config::Theme> getByName(const std::string &name) const;

private:
	std::unordered_map<std::string, Config::Theme>      mThemes;
	std::shared_ptr<const ResolvedTheme>                mActive;
	std::vector<std::string>                            mAvailableNames;
	mutable std::mutex                                  mMutex;        // guards mThemes / mAvailableNames
	EventBus::SubscriptionId                            mEventSubId = 0;

	/** Parse hex string (#RRGGBB or #RGB) into ftxui::Color. */
	ftxui::Color parseHex(const std::string &hex) const noexcept;

	/** Resolve all theme tokens into a fresh ResolvedTheme (no shared state touched). */
	[[nodiscard]] ResolvedTheme resolve(const Config::Theme &theme) const;

	/** Load themes from a single directory. Skips files that fail to parse. */
	void loadFromDirectory(const std::string &path);

	/** Write the internal default theme to <dir>/default.json (overwrites). */
	void seedDefaultTheme(const std::string &dir) const;

	/** Build an ftxui::LinearGradient from gradient stops. */
	ftxui::LinearGradient buildGradient(
		const std::vector<Config::GradientStop> &stops, int angle) const;

	/** Internal hardcoded fallback — always available if no themes load. */
	Config::Theme makeInternalDefault() const;

	/** Event handler for "config.ui.theme" changes from settings panel. */
	static void onThemeChanged(const std::string &eventId, const void *data);
};
