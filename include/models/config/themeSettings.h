#pragma once

#include "json.hpp"
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Config {

/**
 * @file themeSettings.h
 * @brief Theme data model for the global theming system.
 *
 * Defines semantic color tokens and gradient stops that replace hardcoded
 * FTXUI palette colors throughout the UI.  Themes are stored as JSON files
 * on disk and resolved at runtime by ThemeManager into ftxui::Color values.
 */

/**
 * @brief Semantic color tokens for a single theme.
 *
 * Each field maps to one or more hardcoded FTXUI Color usages across
 * the panel UI.  Values are hex strings (e.g. "#3F5") that ThemeManager
 * parses into ftxui::Color at load time.
 */
struct ThemeColors
{
	// === Semantic status colors ===
	std::string success; // toggle on, running, loaded — GreenLight
	std::string warning; // processing, active state — Yellow/YellowLight
	std::string error;	 // not running, unloaded, delete — Red/RedLight
	std::string info;	 // inactive, off — Cyan

	// === UI element colors ===
	std::string label;	   // setting row labels — MagentaLight
	std::string title;	   // window titles — Yellow
	std::string text;	   // default text — terminal default (empty)
	std::string mutedText; // placeholder, filler — GrayDark/GrayLight
	std::string separator; // panel separator — Orange3

	// === Interactive element colors ===
	std::string toggleOn;	 // checkbox on, toggle active — GreenLight
	std::string toggleOff;	 // checkbox off — Cyan
	std::string focusAccent; // focused element accent (new)

	// === Backgrounds ===
	std::string gaugeBg;		 // CPU/GPU gauge label bg — NavyBlue
	std::string tableHeaderBg;	 // presets table header bg — Black
	std::string tableHeaderText; // presets table header text — White

	// === Table alternating rows ===
	std::string tableAltEven; // even row color — CyanLight/Cyan
	std::string tableAltOdd;  // odd row color — MagentaLight

	// === Special purpose ===
	std::string capturingBorder; // terminal capturing border — LightGreen
	std::string activeTabBg;	 // active tab indicator bg — MagentaLight
	std::string selectionBg;	 // active row highlight bg — YellowLight
	std::string subtleLabel;	 // secondary labels — GrayLight
	std::string deleteAction;	 // destructive action buttons — Red/RedLight

  private:
  public:
	/** @brief Return true if the field is required and currently empty. */
	bool isEmpty() const noexcept;
};

/**
 * @brief Gradient stop definition for gauge gradients.
 *
 * Stored as (offset, hex_color) pairs in ThemeGradients vectors.
 * Offset ranges from 0.0 to 1.0 along the gradient axis.
 */
struct GradientStop
{
	double offset;
	std::string color;
};

/**
 * @brief Gauge gradient definitions stored as ordered color stops.
 */
struct ThemeGradients
{
	/// Red → Yellow → Green (low load = green, high = red)
	std::vector<GradientStop> cpuLoad;
	/// Green → Yellow → Red (low usage = green, high = red)
	std::vector<GradientStop> memoryUsage;

  private:
  public:
	/** @brief Validate stops are sorted and within [0,1]. */
	void validate() noexcept;
};

/**
 * @brief Complete theme definition.
 *
 * A theme bundles semantic color tokens and gradient definitions under a
 * display name.  Themes are loaded from JSON files on disk and resolved
 * into ftxui::Color values by ThemeManager.
 */
struct Theme
{
	/// Display name for UI dropdown (e.g. "Default", "Dark")
	std::string name;

	/// Optional description shown in settings tooltip
	std::string description;

	/// Semantic color tokens
	ThemeColors colors;

	/// Gauge gradient definitions
	ThemeGradients gradients;

  private:
  public:
	/**
	 * @brief Ensure no required fields are empty; fall back to defaults.
	 */
	void validate() noexcept;
};

// ============================================================================
// JSON serialization (nlohmann::json ADL — found via Koenig lookup)
// ============================================================================

void to_json(nlohmann::json &j, const Theme &t);
void from_json(const nlohmann::json &j, Theme &t);

} // namespace Config
