/**
 * @file themeSettings.cpp
 * @brief JSON serialization and validation for theme data model.
 *
 * Implements to_json/from_json ADL functions for Theme, ThemeColors,
 * ThemeGradients, and GradientStop.  Validation falls back to hardcoded
 * defaults matching the current app color palette when fields are missing
 * or empty.
 */

#include "themeSettings.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

using json = nlohmann::json;

namespace Config {

// ============================================================================
// Default fallback values — match current hardcoded FTXUI palette colors
// ============================================================================

namespace defaults {

inline const char *success = "#008000"; // Green
inline const char *warning = "#808000"; // Yellow
inline const char *error = "#800000";	// Red
inline const char *info = "#008080";	// Cyan

inline const char *label = "#FF00FF";	  // MagentaLight
inline const char *title = "#808000";	  // Yellow
inline const char *text = "";			  // terminal default
inline const char *mutedText = "#808080"; // GrayDark
inline const char *separator = "#D78700"; // Orange3

inline const char *toggleOn = "#00FF00";	// GreenLight
inline const char *toggleOff = "#008080";	// Cyan
inline const char *focusAccent = "#FFFFFF"; // White

inline const char *gaugeBg = "#00005F";			// NavyBlue
inline const char *tableHeaderBg = "";			// terminal default (transparent)
inline const char *tableHeaderText = "#FFFFFF"; // White

inline const char *tableAltEven = "#008080"; // Cyan
inline const char *tableAltOdd = "#FF00FF";	 // MagentaLight

inline const char *capturingBorder = "#87FF5F"; // LightGreen
inline const char *activeTabBg = "#FF00FF";		// MagentaLight
inline const char *selectionBg = "#FFFF00";		// YellowLight
inline const char *subtleLabel = "#C0C0C0";		// GrayLight
inline const char *deleteAction = "#800000";	// Red

} // namespace defaults

// ============================================================================
// ThemeColors serialization
// ============================================================================

void to_json(json &j, const ThemeColors &v)
{
	j["success"] = v.success;
	j["warning"] = v.warning;
	j["error"] = v.error;
	j["info"] = v.info;
	j["label"] = v.label;
	j["title"] = v.title;
	j["text"] = v.text;
	j["mutedText"] = v.mutedText;
	j["separator"] = v.separator;
	j["toggleOn"] = v.toggleOn;
	j["toggleOff"] = v.toggleOff;
	j["focusAccent"] = v.focusAccent;
	j["gaugeBg"] = v.gaugeBg;
	j["tableHeaderBg"] = v.tableHeaderBg;
	j["tableHeaderText"] = v.tableHeaderText;
	j["tableAltEven"] = v.tableAltEven;
	j["tableAltOdd"] = v.tableAltOdd;
	j["capturingBorder"] = v.capturingBorder;
	j["activeTabBg"] = v.activeTabBg;
	j["selectionBg"] = v.selectionBg;
	j["subtleLabel"] = v.subtleLabel;
	j["deleteAction"] = v.deleteAction;
}

void from_json(const json &j, ThemeColors &v)
{
	v.success = j.value("success", defaults::success);
	v.warning = j.value("warning", defaults::warning);
	v.error = j.value("error", defaults::error);
	v.info = j.value("info", defaults::info);
	v.label = j.value("label", defaults::label);
	v.title = j.value("title", defaults::title);
	v.text = j.value("text", defaults::text);
	v.mutedText = j.value("mutedText", defaults::mutedText);
	v.separator = j.value("separator", defaults::separator);
	v.toggleOn = j.value("toggleOn", defaults::toggleOn);
	v.toggleOff = j.value("toggleOff", defaults::toggleOff);
	v.focusAccent = j.value("focusAccent", defaults::focusAccent);
	v.gaugeBg = j.value("gaugeBg", defaults::gaugeBg);
	v.tableHeaderBg = j.value("tableHeaderBg", defaults::tableHeaderBg);
	v.tableHeaderText = j.value("tableHeaderText", defaults::tableHeaderText);
	v.tableAltEven = j.value("tableAltEven", defaults::tableAltEven);
	v.tableAltOdd = j.value("tableAltOdd", defaults::tableAltOdd);
	v.capturingBorder = j.value("capturingBorder", defaults::capturingBorder);
	v.activeTabBg = j.value("activeTabBg", defaults::activeTabBg);
	v.selectionBg = j.value("selectionBg", defaults::selectionBg);
	v.subtleLabel = j.value("subtleLabel", defaults::subtleLabel);
	v.deleteAction = j.value("deleteAction", defaults::deleteAction);
}

bool ThemeColors::isEmpty() const noexcept
{
	// "text" is allowed to be empty (terminal default)
	return success.empty() || warning.empty() || error.empty() || info.empty() ||
		   label.empty() || title.empty();
}

// ============================================================================
// GradientStop serialization
// ============================================================================

void to_json(json &j, const GradientStop &v)
{
	j = json{ { "offset", std::round(v.offset * 100.0) / 100.0 },
			  { "color", v.color } };
}

void from_json(const json &j, GradientStop &v)
{
	v.offset = j.value("offset", 0.0);
	v.color = j.value("color", "");
}

// ============================================================================
// ThemeGradients serialization + validation
// ============================================================================

void to_json(json &j, const ThemeGradients &v)
{
	j["cpuLoad"] = v.cpuLoad;
	j["memoryUsage"] = v.memoryUsage;
}

void from_json(const json &j, ThemeGradients &v)
{
	if (j.contains("cpuLoad"))
		v.cpuLoad = j["cpuLoad"].get<std::vector<GradientStop>>();
	if (j.contains("memoryUsage"))
		v.memoryUsage = j["memoryUsage"].get<std::vector<GradientStop>>();
	v.validate();
}

void ThemeGradients::validate() noexcept
{
	auto clampAndSort = [](std::vector<GradientStop> &stops) {
		for (auto &s : stops) {
			s.offset = std::clamp(s.offset, 0.0, 1.0);
			if (s.color.empty())
				s.color = "#FFF";
		}
		std::ranges::sort(stops, {}, &GradientStop::offset);
	};

	clampAndSort(cpuLoad);
	clampAndSort(memoryUsage);

	// Ensure at least two stops per gradient for valid LinearGradient
	if (cpuLoad.size() < 2) {
		cpuLoad = { { 0.0, "#F00" }, { 0.5, "#FF0" }, { 1.0, "#0F0" } };
	}
	if (memoryUsage.size() < 2) {
		memoryUsage = { { 0.0, "#0F0" }, { 0.5, "#FF0" }, { 1.0, "#F00" } };
	}
}

// ============================================================================
// Theme serialization + validation
// ============================================================================

void to_json(json &j, const Theme &t)
{
	j["name"] = t.name;
	j["description"] = t.description;
	j["colors"] = t.colors;
	j["gradients"] = t.gradients;
}

void from_json(const json &j, Theme &t)
{
	t.name = j.value("name", "Unnamed");
	t.description = j.value("description", "");
	if (j.contains("colors"))
		t.colors = j["colors"].get<ThemeColors>();
	if (j.contains("gradients"))
		t.gradients = j["gradients"].get<ThemeGradients>();
	t.validate();
}

void Theme::validate() noexcept
{
	// Fallback for empty required color fields
	auto fallbackIfEmpty = [](std::string &field, const char *fallback) {
		if (field.empty())
			field = fallback;
	};

	fallbackIfEmpty(colors.success, defaults::success);
	fallbackIfEmpty(colors.warning, defaults::warning);
	fallbackIfEmpty(colors.error, defaults::error);
	fallbackIfEmpty(colors.info, defaults::info);
	fallbackIfEmpty(colors.label, defaults::label);
	fallbackIfEmpty(colors.title, defaults::title);
	fallbackIfEmpty(colors.mutedText, defaults::mutedText);
	fallbackIfEmpty(colors.separator, defaults::separator);
	fallbackIfEmpty(colors.toggleOn, defaults::toggleOn);
	fallbackIfEmpty(colors.toggleOff, defaults::toggleOff);
	fallbackIfEmpty(colors.focusAccent, defaults::focusAccent);
	fallbackIfEmpty(colors.gaugeBg, defaults::gaugeBg);
	fallbackIfEmpty(colors.tableHeaderBg, defaults::tableHeaderBg);
	fallbackIfEmpty(colors.tableHeaderText, defaults::tableHeaderText);
	fallbackIfEmpty(colors.tableAltEven, defaults::tableAltEven);
	fallbackIfEmpty(colors.tableAltOdd, defaults::tableAltOdd);
	fallbackIfEmpty(colors.capturingBorder, defaults::capturingBorder);
	fallbackIfEmpty(colors.activeTabBg, defaults::activeTabBg);
	fallbackIfEmpty(colors.selectionBg, defaults::selectionBg);
	fallbackIfEmpty(colors.subtleLabel, defaults::subtleLabel);
	fallbackIfEmpty(colors.deleteAction, defaults::deleteAction);

	if (name.empty())
		name = "Unnamed";

	gradients.validate();
}

} // namespace Config
