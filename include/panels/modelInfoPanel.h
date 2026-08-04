#pragma once

#include "IModelInfoMonitor.h"
#include "IVllmMonitor.h"
#include "modelInfoMonitor.h"

#include <ftxui/dom/elements.hpp>
#include <functional>
#include <optional>

/**
 * @file modelInfoPanel.h
 * @brief Stateless panel that renders model metrics from ModelInfoMonitor.
 *
 * Displays:
 * - Currently Loaded Model name
 * - Average Generation Token/s
 * - Average Processing Token/s
 * - Total Prompt Tokens Processed
 * - Total Generation Tokens Processed
 * - Status: Idle / Processing (active request count)
 *
 * The panel is completely stateless; all data is fetched from the
 * ModelInfoMonitor singleton on each render call. All helper methods are static.
 *
 * When a vllmMonitor is provided, renders two columns side by side:
 * "LLAMA" (from IModelInfoMonitor) and "VLLM" (from IVllmMonitor).
 *
 * @note This panel is designed for high-frequency updates (1Hz via
 *       ModelInfoMonitor polling).
 */
class ModelInfoPanel
{
  public:
	/**
	 * @brief Builds and returns the model info panel.
	 *
	 * Takes a monitor reference rather than calling singletons, enabling
	 * unit testing with mocks. The caller (app.cpp) passes singleton instances.
	 *
	 * @param monitor ModelInfoMonitor reference for stats.
	 * @param vllmMonitor Optional vLLM monitor reference. When present, renders
	 * a dual-column layout with "LLAMA" and "VLLM" headers.
	 * @return An @c ftxui::Element containing the fully composed panel.
	 */
	static ftxui::Element
	render(IModelInfoMonitor &monitor,
		   std::optional<std::reference_wrapper<IVllmMonitor>> vllmMonitor =
			   std::nullopt);

  private:
	/**
	 * @brief Formats a double value for display.
	 *
	 * @param value The value to format.
	 * @param precision Number of decimal places.
	 * @return Formatted string.
	 */
	static std::string formatDouble(double value, int precision = 1);

	/**
	 * @brief Formats a uint64_t value with thousand separators.
	 *
	 * @param value The value to format.
	 * @return Formatted string with commas.
	 */
	static std::string formatNumber(uint64_t value);

	/**
	 * @brief Gets the status string based on idle state and request count.
	 *
	 * @param info The ModelInfo to derive status from.
	 * @return Status string ("Idle", "Processing", or "N/A")
	 */
	static std::string getStatusString(const ModelInfo &info);

	/**
	 * @brief Builds the per-model stats column (Model / Generated / Processing /
	 *        Status).
	 *
	 * One vertical block per loaded model; @ref render lays these out
	 * horizontally, one column per model. A default-constructed @p info renders
	 * the offline/none column.
	 *
	 * @param info Stats for a single model.
	 * @param serverName Optional server label (e.g. "LLAMA", "VLLM"). When
	 * provided, a bold header is rendered above the column.
	 * @return An @c ftxui::Element vbox for one model.
	 */
	static ftxui::Element renderModelColumn(const ModelInfo &info,
											const std::string &serverName = "");
};