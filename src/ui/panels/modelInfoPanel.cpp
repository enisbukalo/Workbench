/**
 * @file modelInfoPanel.cpp
 * @brief Implementation of ModelInfoPanel.
 *
 * Stateless panel that renders model metrics from ModelInfoMonitor.
 * Provides a third column view of llama-server model statistics.
 * Supports dual-column layout with vLLM server info.
 */

#include "modelInfoPanel.h"

#include "ThemeManager.h"

#include <ftxui/dom/elements.hpp>
#include <functional>
#include <iomanip>
#include <sstream>

using namespace ftxui;

namespace {
std::string formatDouble(double value, int precision)
{
	std::ostringstream oss;
	oss << std::fixed << std::setprecision(precision) << value;
	return oss.str();
}

std::string formatNumber(uint64_t value)
{
	// Manual thousand separator
	std::string result;
	std::string num = std::to_string(value);
	int count = 0;
	for (int i = static_cast<int>(num.size()) - 1; i >= 0; --i) {
		if (count > 0 && count % 3 == 0) {
			result = ',' + result;
		}
		result = num[i] + result;
		++count;
	}
	return result.empty() ? "0" : result;
}

std::string getStatusString(const ModelInfo &info)
{
	if (!info.isServerRunning || !info.isModelLoaded) {
		return "N/A";
	}
	switch (info.activityState) {
	case ActivityState::GENERATING:
		return "Generating";
	case ActivityState::PROMPT:
		return "Processing";
	case ActivityState::IDLE:
	default:
		return "Idle";
	}
}
} // namespace

std::string ModelInfoPanel::formatDouble(double value, int precision)
{
	return ::formatDouble(value, precision);
}

std::string ModelInfoPanel::formatNumber(uint64_t value)
{
	return ::formatNumber(value);
}

std::string ModelInfoPanel::getStatusString(const ModelInfo &info)
{
	return ::getStatusString(info);
}

Element ModelInfoPanel::renderModelColumn(const ModelInfo &info,
										  const std::string &serverName)
{
	// Format values
	std::string modelName = info.isServerRunning ? info.loadedModel : "N/A";
	if (!info.isModelLoaded && info.isServerRunning) {
		modelName = "None";
	} else if (!info.isServerRunning) {
		modelName = "Server: Offline";
	}

	std::string genTokPerSec = info.isModelLoaded
								   ? formatDouble(info.generationTokensPerSec, 1)
								   : "N/A";
	std::string procTokPerSec =
		info.isModelLoaded ? formatDouble(info.processingTokensPerSec, 1)
						   : "N/A";
	std::string status = getStatusString(info);

	auto theme = ThemeManager::instance().getActive();

	Elements columnElements;

	// Server name header (bold label above the column)
	if (!serverName.empty()) {
		columnElements.push_back(text(serverName) | bold | hcenter);
		columnElements.push_back(separatorLight());
	}

	columnElements.push_back(vbox({
		// Model name
		hbox({
			text("Model: ") | bold | color(theme->label),
			text(modelName) |
				color(info.isModelLoaded ? theme->info : theme->error),
		}),
		separatorLight(),
		// Generation tokens/s
		hbox({
			text("Generated: ") | bold | color(theme->label),
			text(genTokPerSec + " tok/s (AVG)") |
				color(info.isModelLoaded ? theme->warning : theme->error),
		}),
		separatorLight(),
		// Processing tokens/s
		hbox({
			text("Processing: ") | bold | color(theme->label),
			text(procTokPerSec + " tok/s (AVG)") |
				color(info.isModelLoaded ? theme->warning : theme->error),
		}),
		separatorLight(),
		// Status
		hbox({
			text("Status: ") | bold | color(theme->label),
			text(status) | color(info.isIdle ? theme->success : theme->warning),
		}),
	}));

	return vbox(std::move(columnElements));
}

Element ModelInfoPanel::render(
	IModelInfoMonitor &monitor,
	std::optional<std::reference_wrapper<IVllmMonitor>> vllmMonitor)
{
	// Build the LLAMA column(s) from the llama.cpp monitor.
	Elements llamaColumns;
	{
		auto models = monitor.getAllStats();

		// No models loaded: render a single column reflecting the aggregate
		// state (offline vs server-up-but-none).
		if (models.empty()) {
			llamaColumns.push_back(
				renderModelColumn(monitor.getStats(),
								  vllmMonitor ? "LLAMA" : ""));
		} else {
			// One column per loaded model, grown horizontally and separated by
			// a rule.
			bool first = true;
			for (const auto &[id, info] : models) {
				if (!first)
					llamaColumns.push_back(separator());
				llamaColumns.push_back(
					renderModelColumn(info, vllmMonitor ? "LLAMA" : ""));
				first = false;
			}
		}
	}

	// If no vllmMonitor, return just the llama columns.
	if (!vllmMonitor) {
		return hbox(std::move(llamaColumns));
	}

	// Build the VLLM column from the vLLM monitor.
	ModelInfo vllmInfo = vllmMonitor->get().getStats();
	Element vllmColumn = renderModelColumn(vllmInfo, "VLLM");

	// Combine: LLAMA columns | separator | VLLM column.
	Elements allColumns;
	allColumns.insert(allColumns.end(),
					  llamaColumns.begin(),
					  llamaColumns.end());
	allColumns.push_back(separator());
	allColumns.push_back(std::move(vllmColumn));

	return hbox(std::move(allColumns));
}