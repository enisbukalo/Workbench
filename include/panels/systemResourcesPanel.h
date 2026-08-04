#pragma once

#include "ICpuMonitor.h"
#include "IGpuMonitor.h"
#include "IMemoryMonitor.h"
#include "IModelInfoMonitor.h"
#include "IVllmMonitor.h"
#include "memoryStats.h"
#include "modelInfoPanel.h"
#include "processorStats.h"

#include <ftxui/dom/elements.hpp>
#include <functional>
#include <optional>
#include <vector>

/**
 * @file systemResourcesPanel.h
 * @brief Stateless panel that renders live resource-usage gauges and a memory
 * table.
 *
 * This panel provides a comprehensive view of system resources, including:
 * - CPU load gauge: Vertical bar showing current CPU utilization
 * - GPU load gauges: One vertical bar per detected GPU
 * - Memory table: Tabular display of RAM and VRAM usage with:
 *   - Total capacity
 *   - Used memory
 *   - Available memory
 *   - Usage percentage
 *   - Color-coded gauge indicator
 * - Total memory column: Aggregated RAM + all GPU VRAM
 *
 * The panel is completely stateless; all data is fetched from the
 * monitor singletons (CpuMonitor, MemoryMonitor, GpuMonitor) on
 * each render call. All helper methods are static.
 *
 * @note This panel is designed for high-frequency updates (typically
 *       2Hz via SystemMonitorRunner) and minimizes allocations
 *       during rendering for performance.
 */
class SystemResourcesPanel
{
  public:
	/**
	 * @brief Builds and returns the complete resource-usage panel.
	 *
	 * Takes monitor references rather than calling singletons, enabling
	 * unit testing with mocks. The caller (app.cpp) passes singleton instances.
	 *
	 * @param cpu CPU monitor reference for load stats.
	 * @param mem Memory monitor reference for RAM stats.
	 * @param gpu GPU monitor reference for VRAM and compute stats.
	 * @param modelInfo Model info monitor reference for the embedded
	 * ModelInfoPanel.
	 * @param vllmMonitor Optional vLLM monitor reference. When present, the
	 * ModelInfoPanel renders a dual-column layout (LLAMA + VLLM).
	 * @param cpuLowC Optional CPU green threshold (°C). When nullopt, read from
	 * ConfigManager. Allows tests to override the singleton.
	 * @param cpuHiC Optional CPU red threshold (°C). When nullopt, read from
	 * ConfigManager. Allows tests to override the singleton.
	 * @param gpuLowC Optional GPU green threshold (°C). When nullopt, read from
	 * ConfigManager. Allows tests to override the singleton.
	 * @param gpuHiC Optional GPU red threshold (°C). When nullopt, read from
	 * ConfigManager. Allows tests to override the singleton.
	 * @return An @c ftxui::Element containing the fully composed panel.
	 */
	static ftxui::Element
	render(ICpuMonitor &cpu,
		   IMemoryMonitor &mem,
		   IGpuMonitor &gpu,
		   IModelInfoMonitor &modelInfo,
		   std::optional<std::reference_wrapper<IVllmMonitor>> vllmMonitor =
			   std::nullopt,
		   std::optional<double> cpuLowC = std::nullopt,
		   std::optional<double> cpuHiC = std::nullopt,
		   std::optional<double> gpuLowC = std::nullopt,
		   std::optional<double> gpuHiC = std::nullopt);

  private:
	/**
	 * @brief Builds table row data for system RAM usage.
	 *
	 * @param stats The current @c MemoryStats snapshot for system RAM.
	 * @return A 2-D vector of @c ftxui::Element rows suitable for a table
	 * widget.
	 */
	static std::vector<std::vector<ftxui::Element>>
	buildRamRows(const MemoryStats &stats);

	/**
	 * @brief Builds table row data for each GPU's VRAM usage.
	 *
	 * @param stats A vector of @c MemoryStats snapshots, one per GPU.
	 * @return A 2-D vector of @c ftxui::Element rows suitable for a table
	 * widget.
	 */
	static std::vector<std::vector<ftxui::Element>>
	buildGpuRows(const std::vector<MemoryStats> &stats);

	/**
	 * @brief Builds the column-header row for the memory table.
	 *
	 * Generates labels for RAM and each detected GPU (e.g. "GPU 0", "GPU 1", …).
	 *
	 * @param gpu_stats A vector of GPU @c MemoryStats used to determine the GPU
	 * count.
	 * @return A vector of @c ftxui::Element column-header cells.
	 */
	static std::vector<ftxui::Element>
	buildHeaderRow(const std::vector<MemoryStats> &gpu_stats);

	/**
	 * @brief Builds a CPU load gauge with its temperature beneath it.
	 *
	 * The gauge sits in a column with the heat-graded temperature number
	 * centered directly under it (GreenLight -> Red by Celsius; "N/A" when
	 * unavailable). The green/red thresholds are read from config.
	 *
	 * @param stats        The current @c ProcessorStats snapshot for the CPU.
	 * @param fahrenheit   When true, the temperature is shown in Fahrenheit.
	 * @param lowC         Green threshold (°C) — green at/below this value.
	 * @param hiC          Red threshold (°C) — red at/above this value.
	 * @return An @c ftxui::Element containing the CPU gauge over its temp.
	 */
	static ftxui::Element buildCpuGauge(const ProcessorStats &stats,
										bool fahrenheit,
										double lowC,
										double hiC);

	/**
	 * @brief Builds per-GPU load gauges, each with its temperature beneath it.
	 *
	 * @param stats        A vector of @c ProcessorStats snapshots, one per GPU.
	 * @param fahrenheit   When true, temperatures are shown in Fahrenheit.
	 * @param lowC         Green threshold (°C) — green at/below this value.
	 * @param hiC          Red threshold (°C) — red at/above this value.
	 * @return An @c ftxui::Element with one gauge-over-temp column per GPU.
	 */
	static ftxui::Element
	buildGpuGauges(const std::vector<ProcessorStats> &stats,
				   bool fahrenheit,
				   double lowC,
				   double hiC);

	/**
	 * @brief Builds the rightmost units column for the memory table.
	 *
	 * Contains unit labels such as "GB" and "%" to annotate memory table rows.
	 *
	 * @return A vector of @c ftxui::Element unit-label cells.
	 */
	static std::vector<ftxui::Element> buildUnitsColumn();

	/**
	 * @brief Builds the total memory column (RAM + all GPUs aggregated).
	 *
	 * Aggregates system RAM and all GPU memory into a single column showing
	 * combined totals, usage, and available memory.
	 *
	 * @param gpuStats A vector of GPU MemoryStats snapshots.
	 * @param ramStats The current MemoryStats snapshot for system RAM.
	 * @return A 2-D vector of ftxui::Element rows (5 rows: Total, Used, Avail,
	 * Usage, Gauge).
	 */
	static std::vector<ftxui::Element>
	buildTotalMemoryColumn(const std::vector<MemoryStats> &gpuStats,
						   const MemoryStats &ramStats);
};