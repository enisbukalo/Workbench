/**
 * @file systemResourcesPanel.cpp
 * @brief System resources panel implementation.
 *
 * Implements the main system monitoring panel that displays CPU/GPU load
 * gauges and a comprehensive memory usage table for RAM and all GPUs.
 */

#include "systemResourcesPanel.h"

#include "ThemeManager.h"
#include "configManager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/table.hpp>
#include <functional>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

using namespace ftxui;

namespace {

/**
 * @brief Format a Celsius reading for display beneath a load gauge.
 *
 * The -1.0 sentinel renders as "N/A". A valid reading is converted to
 * Fahrenheit when requested and rendered as a rounded integer with a
 * lowercase unit suffix (e.g. "65c" / "149f").
 */
std::string formatTemp(double celsius, bool fahrenheit)
{
	if (celsius < 0.0)
		return "N/A";
	double v = fahrenheit ? (celsius * 9.0 / 5.0 + 32.0) : celsius;
	char unit = fahrenheit ? 'f' : 'c';
	return std::to_string(static_cast<int>(std::lround(v))) + unit;
}

/**
 * @brief Heat-graded color for a temperature, always in Celsius.
 *
 * @p lowC renders GreenLight, @p hiC renders Red, and in between the color is
 * a linear GreenLight -> Red interpolation. The thresholds are on the raw
 * Celsius reading regardless of the display unit. The -1.0 sentinel (no
 * reading) gets GreenLight, but its text is "N/A" so the color is moot.
 *
 * When lowC >= hiC (equal thresholds), GreenLight is returned to avoid
 * division by zero in the interpolation formula.
 *
 * @param celsius Temperature reading in Celsius.
 * @param lowC    Green threshold (°C) — at or below renders green.
 * @param hiC     Red threshold (°C) — at or above renders red.
 * @return An interpolated ftxui::Color from green to red.
 */
ftxui::Color tempColor(double celsius, double lowC, double hiC)
{
	// Guard: when thresholds are equal, return green (no interpolation range).
	// This prevents division by zero in the interpolation formula below.
	if (lowC >= hiC)
		return ftxui::Color::GreenLight;

	// ftxui GreenLight ~ (95,255,95); Red = (255,0,0).
	constexpr int lowR = 95, lowG = 255, lowB = 95;
	constexpr int hiR = 255, hiG = 0, hiB = 0;

	double t = (celsius - lowC) / (hiC - lowC);
	t = std::clamp(t, 0.0, 1.0);
	auto lerp = [t](int a, int b) {
		return static_cast<uint8_t>(std::lround(a + (b - a) * t));
	};
	return ftxui::Color::RGB(lerp(lowR, hiR), lerp(lowG, hiG), lerp(lowB, hiB));
}

} // namespace

std::vector<std::vector<Element>>
SystemResourcesPanel::buildRamRows(const MemoryStats &ramStats)
{
	std::ostringstream ossTotal, ossUsed, ossAvail, ossPct;
	ossTotal << std::fixed << std::setprecision(2)
			 << (ramStats.totalMb / 1024.0);
	ossUsed << std::fixed << std::setprecision(2) << (ramStats.usedMb / 1024.0);
	ossAvail << std::fixed << std::setprecision(2)
			 << (ramStats.availableMb / 1024.0);
	ossPct << std::fixed << std::setprecision(2) << ramStats.usagePercentage;

	auto theme = ThemeManager::instance().getActive();

	Element gauge = gaugeRight(ramStats.usagePercentage / 100.0f) |
					color(theme->memoryUsageGauge);

	std::vector<std::vector<Element>> rows = {
		{ text("Total") | bold, text(ossTotal.str()) },
		{ text("Used") | bold, text(ossUsed.str()) },
		{ text("Avail") | bold, text(ossAvail.str()) },
		{ text("Usage") | bold, text(ossPct.str()) },
		{ text(""), gauge },
	};

	return rows;
}

std::vector<std::vector<Element>>
SystemResourcesPanel::buildGpuRows(const std::vector<MemoryStats> &gpuStats)
{
	std::vector<std::vector<Element>> rows(5);
	for (size_t i = 0; i < gpuStats.size(); ++i) {
		const auto &stats = gpuStats[i];
		std::ostringstream ossTotal, ossUsed, ossAvail, ossPct;
		ossTotal << std::fixed << std::setprecision(2)
				 << (stats.totalMb / 1024.0);
		ossUsed << std::fixed << std::setprecision(2) << (stats.usedMb / 1024.0);
		ossAvail << std::fixed << std::setprecision(2)
				 << (stats.availableMb / 1024.0);
		ossPct << std::fixed << std::setprecision(2) << stats.usagePercentage;

		auto theme = ThemeManager::instance().getActive();

		Element gauge = gaugeRight(stats.usagePercentage / 100.0f) |
						color(theme->memoryUsageGauge);

		rows[0].push_back(text(ossTotal.str()));
		rows[1].push_back(text(ossUsed.str()));
		rows[2].push_back(text(ossAvail.str()));
		rows[3].push_back(text(ossPct.str()));
		rows[4].push_back(gauge);
	}
	return rows;
}

namespace {

/**
 * @brief Plain heat-graded temperature line, centered to a gauge's width.
 *
 * No gauge/border — just the number, styled like the other panel numbers but
 * colored GreenLight->Red by temperature. The "N/A" sentinel stays uncolored
 * so it reads as "no data" rather than "cold".
 *
 * @param celsius  Temperature reading in Celsius (-1.0 = unavailable).
 * @param fahrenheit When true, display in Fahrenheit.
 * @param lowC     Green threshold (°C).
 * @param hiC      Red threshold (°C).
 * @return An ftxui::Element with the formatted temperature.
 */
Element buildTempLine(double celsius, bool fahrenheit, double lowC, double hiC)
{
	auto el = text(formatTemp(celsius, fahrenheit)) | hcenter;
	if (celsius >= 0.0)
		el = el | color(tempColor(celsius, lowC, hiC));
	return el;
}

} // namespace

Element SystemResourcesPanel::buildCpuGauge(const ProcessorStats &processorStats,
											bool fahrenheit,
											double lowC,
											double hiC)
{
	auto theme = ThemeManager::instance().getActive();

	Element gaugeBox =
		hbox({
			vtext("CPU") | vcenter | hcenter | bgcolor(theme->gaugeBg),
			separatorLight(),
			gaugeUp(processorStats.usagePercentage / 100.0) |
				ftxui::color(theme->cpuLoadGauge) | yflex,
		}) |
		borderRounded;

	// The gauge flexes to the row's tallest column so every gauge ends the same
	// height, and the temperature is pinned to the bottom so all numbers line up
	// on one baseline regardless of gauge height.
	return vbox({
		gaugeBox | yflex,
		buildTempLine(processorStats.temperatureC, fahrenheit, lowC, hiC),
	});
}

ftxui::Element SystemResourcesPanel::buildGpuGauges(
	const std::vector<ProcessorStats> &gpuLoadStats,
	bool fahrenheit,
	double lowC,
	double hiC)
{
	if (gpuLoadStats.empty())
		return ftxui::text("No GPU");

	auto theme = ThemeManager::instance().getActive();

	Elements columns;
	for (size_t i = 0; i < gpuLoadStats.size(); ++i) {
		Element gaugeBox = hbox({
							   vtext("GPU" + std::to_string(i)) | vcenter |
								   hcenter | bgcolor(theme->gaugeBg),
							   separatorLight(),
							   gaugeUp(gpuLoadStats[i].usagePercentage / 100.0) |
								   ftxui::color(theme->cpuLoadGauge) | yflex,
						   }) |
						   borderRounded;

		columns.push_back(vbox({
			gaugeBox | yflex,
			buildTempLine(gpuLoadStats[i].temperatureC, fahrenheit, lowC, hiC),
		}));
	}
	return hbox(std::move(columns));
}

std::vector<ftxui::Element> SystemResourcesPanel::buildTotalMemoryColumn(
	const std::vector<MemoryStats> &gpuStats,
	const MemoryStats &ramStats)
{
	// Calculate totals
	uint64_t totalMb = ramStats.totalMb;
	uint64_t usedMb = ramStats.usedMb;
	uint64_t availableMb = ramStats.availableMb;

	for (const auto &gpu : gpuStats) {
		totalMb += gpu.totalMb;
		usedMb += gpu.usedMb;
		availableMb += gpu.availableMb;
	}

	// Calculate usage percentage
	double usagePercentage = 0.0;
	if (totalMb > 0) {
		usagePercentage =
			(static_cast<double>(usedMb) / static_cast<double>(totalMb)) * 100.0;
	}

	// Format values
	std::ostringstream ossTotal, ossUsed, ossAvail, ossPct;
	ossTotal << std::fixed << std::setprecision(2) << (totalMb / 1024.0);
	ossUsed << std::fixed << std::setprecision(2) << (usedMb / 1024.0);
	ossAvail << std::fixed << std::setprecision(2) << (availableMb / 1024.0);
	ossPct << std::fixed << std::setprecision(2) << usagePercentage;

	auto theme = ThemeManager::instance().getActive();

	// Build gauge
	Element gauge =
		gaugeRight(usagePercentage / 100.0f) | color(theme->memoryUsageGauge);

	// Return column as transposed (5 rows, 1 column each)
	return { text(ossTotal.str()),
			 text(ossUsed.str()),
			 text(ossAvail.str()),
			 text(ossPct.str()),
			 gauge };
}

std::vector<Element>
SystemResourcesPanel::buildHeaderRow(const std::vector<MemoryStats> &gpuStats)
{
	std::vector<Element> header;
	header.push_back(text("")); // Empty for label column
	header.push_back(text("RAM") | bold);
	for (const auto &gpu : gpuStats) {
		header.push_back(text("GPU " + std::to_string(gpu.id)) | bold);
	}
	header.push_back(text("TOTAL") | bold);
	return header;
}

std::vector<Element> SystemResourcesPanel::buildUnitsColumn()
{
	return {
		text(""),		   // Empty for label
		text("GB") | bold, // Total
		text("GB") | bold, // Used
		text("GB") | bold, // Avail
		text("%") | bold,  // Usage
		text(""),		   // gauge (empty)
	};
}

Element SystemResourcesPanel::render(
	ICpuMonitor &cpu,
	IMemoryMonitor &mem,
	IGpuMonitor &gpu,
	IModelInfoMonitor &modelInfo,
	std::optional<std::reference_wrapper<IVllmMonitor>> vllmMonitor,
	std::optional<double> cpuLowC,
	std::optional<double> cpuHiC,
	std::optional<double> gpuLowC,
	std::optional<double> gpuHiC)
{
	auto ramStats = mem.getStats();
	auto gpuStats = gpu.getStats();
	auto processorStats = cpu.getStats();
	auto gpuLoadStats = gpu.getLoadStats();

	auto ramRows = buildRamRows(ramStats);
	auto gpuRows = buildGpuRows(gpuStats);
	auto headerRow = buildHeaderRow(gpuStats);
	auto unitsColumn = buildUnitsColumn();
	// Per-device thresholds: read fresh each frame; the panel re-renders at 2Hz
	// so a Settings change is picked up on the next frame without an EventBus
	// hop. When optional thresholds are provided (e.g. by tests), use them
	// directly; otherwise read from ConfigManager singleton.
	double cpuGreen, cpuRed, gpuGreen, gpuRed;
	bool useFahrenheit;
	if (cpuLowC.has_value() && cpuHiC.has_value() && gpuLowC.has_value() &&
		gpuHiC.has_value()) {
		cpuGreen = *cpuLowC;
		cpuRed = *cpuHiC;
		gpuGreen = *gpuLowC;
		gpuRed = *gpuHiC;
		// Still need the config for the temperature unit.
		auto cfg = ConfigManager::instance().getConfigSnapshot();
		useFahrenheit = cfg.ui.temperatureUnit == "fahrenheit";
	} else {
		auto cfg = ConfigManager::instance().getConfigSnapshot();
		useFahrenheit = cfg.ui.temperatureUnit == "fahrenheit";
		cpuGreen = static_cast<double>(cfg.ui.cpuTemperatureGreenBottom);
		cpuRed = static_cast<double>(cfg.ui.cpuTemperatureRedTop);
		gpuGreen = static_cast<double>(cfg.ui.gpuTemperatureGreenBottom);
		gpuRed = static_cast<double>(cfg.ui.gpuTemperatureRedTop);
	}
	auto cpuLoadGauge =
		buildCpuGauge(processorStats, useFahrenheit, cpuGreen, cpuRed);
	auto gpuLoadGauges =
		buildGpuGauges(gpuLoadStats, useFahrenheit, gpuGreen, gpuRed);
	auto totalMemoryColumn = buildTotalMemoryColumn(gpuStats, ramStats);

	// Insert header row at the beginning
	ramRows.insert(ramRows.begin(), headerRow);

	// Append GPU columns to RAM rows
	for (size_t i = 1; i < ramRows.size() && i < gpuRows.size() + 1; ++i) {
		for (auto &el : gpuRows[i - 1]) {
			ramRows[i].push_back(std::move(el));
		}
	}

	// Add total memory column after GPU columns (skip header row at index 0)
	for (size_t i = 1; i < ramRows.size(); ++i) {
		ramRows[i].push_back(totalMemoryColumn[i - 1]);
	}

	// Add units column after RAM, GPU, and total columns
	for (size_t i = 0; i < ramRows.size(); ++i) {
		ramRows[i].push_back(unitsColumn[i]);
	}

	// This is here in case we want to add padding
	auto padding = [](Element e) { return hbox({ text(" "), e, text(" ") }); };

	Table table(ramRows);
	table.SelectAll().DecorateCells(padding);
	auto allCells = table.SelectAll();
	allCells.SeparatorVertical(DASHED);

	// Apply alternating row colors from theme to data rows (excluding header)
	auto theme = ThemeManager::instance().getActive();

	auto dataRows =
		table.SelectRows(1, -1); // Select rows 1 to end (skip header)
	dataRows.DecorateCellsAlternateRow(color(theme->tableAltOdd), 2, 1);
	dataRows.DecorateCellsAlternateRow(color(theme->tableAltEven), 2, 0);

	table.SelectAll().BorderBottom(LIGHT);

	return window(text("System Resources") | bold,
				  hbox({
					  vbox({
						  text("Memory") | bold | hcenter,
						  separatorLight(),
						  table.Render(),
					  }),
					  separatorHeavy(),
					  vbox({
						  text("Load") | hcenter | bold,
						  separatorLight(),
						  hbox({
							  cpuLoadGauge,
							  // separatorLight(),
							  gpuLoadGauges,
						  }),
					  }),
					  separatorLight(),
					  filler(),
					  separatorLight(),
					  vbox({
						  text("Server Info") | bold | hcenter,
						  separatorLight(),
						  ModelInfoPanel::render(modelInfo, vllmMonitor),
					  }),
				  }));
}
