/**
 * @file serverInfoPanel.cpp
 * @brief Server status display showing llama-server log output.
 *
 * Displays the current status of the llama-server process and reads
 * the log file from .workbench/logs/ to show server output.
 */

#include "serverInfoPanel.h"

#include "ThemeManager.h"

#include <ftxui/dom/elements.hpp>

#include <fstream>
#include <sstream>

using namespace ftxui;

namespace {
/**
 * @brief Read the last N lines from a file.
 * @param path Path to the file to read.
 * @param maxLines Maximum number of lines to return.
 * @return The last N lines as a string, or empty if file doesn't exist.
 */
std::string readLastLines(const std::string &path, int maxLines = 10)
{
	std::ifstream file(path);
	if (!file.is_open()) {
		return "";
	}

	// Read all lines into a vector
	std::vector<std::string> lines;
	std::string line;
	while (std::getline(file, line)) {
		lines.push_back(line);
	}

	// Return the last maxLines
	int start = static_cast<int>(lines.size()) - maxLines;
	if (start < 0)
		start = 0;

	std::string result;
	for (int i = start; i < static_cast<int>(lines.size()); ++i) {
		result += lines[i] + "\n";
	}
	return result;
}
} // namespace

Element ServerInfoPanel::render(ILlamaServerProcess &server)
{
	// Check if server is running
	bool running = server.isRunning();

	// Get color based on status from theme
	auto theme = ThemeManager::instance().getActive();
	Color statusColor = running ? theme->success : theme->error;

	// Just show the bullseye indicator - no text
	return hbox({
		text("Server Status") | bold,
		separatorEmpty(),
		text("\u25C9") | hcenter | color(statusColor),
	});
}