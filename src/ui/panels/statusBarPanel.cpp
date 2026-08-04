/**
 * @file statusBarPanel.cpp
 * @brief StatusBarPanel render + time-based hint rotation.
 */

#include "statusBarPanel.h"

#include "serverInfoPanel.h"

#include <ftxui/screen/color.hpp>

using namespace ftxui;

Element StatusBarPanel::render(ILlamaServerProcess &server,
							   std::string_view activeTabName) const
{
	return renderAt(server, activeTabName, std::chrono::steady_clock::now());
}

Element StatusBarPanel::renderAt(ILlamaServerProcess &server,
								 std::string_view activeTabName,
								 std::chrono::steady_clock::time_point now) const
{
	Elements row;

	// Left: server status (reuse ServerInfoPanel — plain hbox, no filler()).
	row.push_back(ServerInfoPanel::render(server));

	// Center: rolling hint, rotated by elapsed wall-clock time. Skip if empty.
	if (!m_config.hints.empty()) {
		const auto elapsedMs =
			std::chrono::duration_cast<std::chrono::milliseconds>(now - m_start)
				.count();
		// hintIntervalMs is clamped >= 1 by validate(), so no divide-by-zero.
		const auto hintIdx = static_cast<std::size_t>(
			(elapsedMs / m_config.hintIntervalMs) %
			static_cast<long long>(m_config.hints.size()));

		row.push_back(separator());
		row.push_back(text(m_config.hints[hintIdx]));
	}

	row.push_back(filler());

	// Right: per-tab keymap hints. Heterogeneous lookup via std::less<> — no
	// temp std::string. Use find(), never operator[] (would insert on miss).
	auto it = m_config.keymaps.find(activeTabName);
	if (it != m_config.keymaps.end() && !it->second.empty()) {
		bool first = true;
		for (const auto &hint : it->second) {
			if (!first)
				row.push_back(separatorEmpty());
			first = false;
			row.push_back(text(hint) | dim);
		}
	}

	return hbox(std::move(row));
}
