#pragma once

#include "ILlamaServerProcess.h"
#include "statusBarSettings.h"

#include <ftxui/dom/elements.hpp>

#include <chrono>
#include <cstddef>
#include <optional>
#include <string_view>

/**
 * @file statusBarPanel.h
 * @brief Bottom status bar: server status + rolling hints + per-tab keymaps.
 *
 * Unlike ServerInfoPanel (stateless static), this panel owns its config plus a
 * rotation epoch, so it is an instance owned by app.cpp. Hint rotation is a pure
 * function of elapsed wall-clock time (steady_clock) — NOT a render-call
 * counter, because FTXUI may call Render multiple times per visual frame, which
 * would make a counter-based scheme drift. render() reads the clock internally;
 * renderAt() is the deterministic test seam.
 */
class StatusBarPanel
{
  public:
	/**
	 * @brief Construct from loaded settings.
	 *
	 * Sink parameter: caller passes an rvalue from the loader — zero copies,
	 * one move. Captures the rotation epoch (steady_clock::now()) so hint
	 * rotation is measured from construction.
	 *
	 * @param cfg Status-bar settings (moved into the panel).
	 */
	explicit StatusBarPanel(Config::StatusBarSettings cfg)
		: m_config(std::move(cfg)), m_start(std::chrono::steady_clock::now())
	{
	}

	/**
	 * @brief Test seam — inject rotation epoch for deterministic hint tests.
	 *
	 * Production code uses the default constructor (no injection). Tests pass
	 * a fixed time_point so elapsed math is exact regardless of clock
	 * resolution.
	 *
	 * @param cfg  Status-bar settings (moved into the panel).
	 * @param start Override for m_start (only used by tests).
	 */
	StatusBarPanel(Config::StatusBarSettings cfg,
				   std::chrono::steady_clock::time_point start) noexcept
		: m_config(std::move(cfg)), m_start(start)
	{
	}

	/**
	 * @brief Build the bottom-row element (reads the clock internally).
	 *
	 * Const-safe: no internal mutation. Equivalent to
	 * renderAt(server, activeTabName, steady_clock::now()).
	 *
	 * @param server        Server reference for the status indicator.
	 * @param activeTabName  Active tab name; selects keymap hints
	 *                       (heterogeneous lookup, no temp string).
	 * @return Composable ftxui::Element for the status bar row.
	 */
	[[nodiscard]] ftxui::Element render(ILlamaServerProcess &server,
										std::string_view activeTabName) const;

	/**
	 * @brief Build the bottom-row element at an explicit clock value.
	 *
	 * Test seam: inject @p now so hint rotation is deterministic without
	 * sleeping. Hint index = ((now - m_start) / hintIntervalMs) % hints.size().
	 *
	 * @param server        Server reference for the status indicator.
	 * @param activeTabName  Active tab name; selects keymap hints.
	 * @param now           Clock value used for rotation math.
	 * @return Composable ftxui::Element for the status bar row.
	 */
	[[nodiscard]] ftxui::Element
	renderAt(ILlamaServerProcess &server,
			 std::string_view activeTabName,
			 std::chrono::steady_clock::time_point now) const;

  private:
	Config::StatusBarSettings m_config;
	std::chrono::steady_clock::time_point m_start;
};
