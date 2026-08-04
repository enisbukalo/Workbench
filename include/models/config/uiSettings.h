#pragma once
#include <algorithm>
#include <string>

namespace Config {

/**
 * @file uiSettings.h
 * @brief UI configuration settings for the Workbench terminal interface.
 *
 * This header defines the UISettings structure which controls the
 * appearance and behavior of the terminal user interface.
 */

/**
 * @brief UI/settings panel settings.
 *
 * Controls the appearance and behavior of the terminal UI:
 * - **Theme**: Visual theme selection for colors and styling
 * - **Default tab**: Which tab opens on startup
 * - **Show system panel**: Toggle system monitor visibility
 * - **Refresh rate**: How often to update monitoring data
 *
 * @note The defaultTab values correspond to tab indices in the
 *       main UI window. The mapping may change as new tabs are added.
 *
 * @code
 * // Configure UI for development
 * UISettings ui;
 * ui.theme = "dark";
 * ui.defaultTab = 1;  // Open Server Log by default
 * ui.refreshRateMs = 100;  // Fast updates for monitoring
 * @endcode
 */
struct UISettings
{
	/**
	 * @brief Visual theme name.
	 *
	 * Determines colors, fonts, and styling throughout the UI.
	 * Available themes depend on the theme system implementation.
	 *
	 * @default "default"
	 * @note Common theme names: "default", "dark", "light", "monokai"
	 */
	std::string theme = "default";

	/**
	 * @brief Default tab to show on startup.
	 *
	 * The tab index that opens when the application starts:
	 * - 0: Settings tab
	 * - 1: Server Log tab
	 * - 2: Terminal tab
	 *
	 * Corresponds to: Tab index in main UI window
	 * @default 0 (Settings)
	 * @note Tab indices may change as new tabs are added to the UI.
	 */
	int defaultTab = 0;

	/**
	 * @brief Show system monitoring panel.
	 *
	 * When enabled, displays real-time system metrics including:
	 * - CPU usage
	 * - Memory usage
	 * - GPU utilization (if available)
	 * - Network activity
	 *
	 * @default true
	 * @note Disabling may improve performance on low-end systems.
	 */
	bool showSystemPanel = true;

	/**
	 * @brief Refresh rate for monitoring data in milliseconds.
	 *
	 * How frequently the system panel and other dynamic elements
	 * update. Lower values = smoother but more CPU intensive.
	 *
	 * @default 250 (4 times per second)
	 * @range 50-1000 recommended
	 * @note Values below 100ms may cause noticeable CPU usage.
	 */
	int refreshRateMs = 250;

	/**
	 * @brief Log file retention period in days.
	 *
	 * Log files older than this many days are deleted on startup.
	 * Set to 0 to disable automatic log cleanup.
	 *
	 * @default 7 (one week)
	 * @range 0-365
	 */
	int logRetentionDays = 7;

	/**
	 * @brief Temperature display unit.
	 *
	 * Controls how CPU/GPU temperatures are shown in the System Resources
	 * panel. Stored as a string and mapped to a dropdown index at the panel
	 * layer (mirrors the server.pooling/reasoning string-enum pattern).
	 *
	 * @default "celsius"
	 * @note Valid values: "celsius", "fahrenheit". Invalid values reset to
	 *       "celsius" in validate().
	 */
	std::string temperatureUnit = "celsius";

	/**
	 * @brief System Resources Only mode.
	 *
	 * When enabled, the application renders only the System Resources panel
	 * — all tabs, toggles, and the status bar are hidden. The user gets a
	 * clean monitoring dashboard. Toggled via checkbox in App Settings or
	 * the Ctrl+U keyboard shortcut.
	 *
	 * @default false
	 */
	bool systemResourcesOnly = false;

	/**
	 * @brief CPU green (cool) temperature threshold in °C.
	 *
	 * Temperatures at or below this value render green for the CPU gauge in the
	 * System Resources panel.
	 *
	 * @default 30
	 * @range -50 to 200 (clamped in validate())
	 */
	int cpuTemperatureGreenBottom = 30;

	/**
	 * @brief CPU red (hot) temperature threshold in °C.
	 *
	 * Temperatures at or above this value render red for the CPU gauge in the
	 * System Resources panel.
	 *
	 * @default 80
	 * @range -50 to 200 (clamped in validate())
	 * @note If green > red after user input, validate() swaps them.
	 */
	int cpuTemperatureRedTop = 80;

	/**
	 * @brief GPU green (cool) temperature threshold in °C.
	 *
	 * Temperatures at or below this value render green for GPU gauges in the
	 * System Resources panel.
	 *
	 * @default 40
	 * @range -50 to 200 (clamped in validate())
	 */
	int gpuTemperatureGreenBottom = 40;

	/**
	 * @brief GPU red (hot) temperature threshold in °C.
	 *
	 * Temperatures at or above this value render red for GPU gauges in the
	 * System Resources panel.
	 *
	 * @default 90
	 * @range -50 to 200 (clamped in validate())
	 * @note If green > red after user input, validate() swaps them.
	 */
	int gpuTemperatureRedTop = 90;

	/**
	 * @brief Clamp all numeric fields to valid ranges in-place.
	 *
	 * Called automatically at the end of from_json.
	 */
	void validate() noexcept;
};

} // namespace Config
