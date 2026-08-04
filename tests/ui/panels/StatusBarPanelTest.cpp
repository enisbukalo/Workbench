/**
 * @file StatusBarPanelTest.cpp
 * @brief Tests for StatusBarPanel render output and time-based hint rotation.
 *
 * Rotation is driven via the renderAt(now) seam so tests are deterministic
 * (no sleeps). Rendered output is captured to a string via an FTXUI Screen.
 */

#include "statusBarPanel.h"

#include "MockLlamaServerProcess.h"

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <string>

using namespace testing;
using namespace std::chrono;

namespace {
/// Render an element to a flat string for substring assertions.
std::string renderToString(ftxui::Element el)
{
	auto screen = ftxui::Screen::Create(ftxui::Dimension::Fit(el));
	ftxui::Render(screen, el);
	return screen.ToString();
}

Config::StatusBarSettings makeConfig()
{
	Config::StatusBarSettings cfg;
	cfg.hintIntervalMs = 1000;
	cfg.hints = { "HINT_ZERO", "HINT_ONE", "HINT_TWO" };
	cfg.keymaps = { { "Terminal", { "KEYMAP_TERMINAL" } } };
	return cfg;
}
} // namespace

class StatusBarPanelTest : public Test
{
  protected:
	NiceMock<MockLlamaServerProcess> mockServer;
	steady_clock::time_point base = steady_clock::now();
};

// ============================================================================
// Basic render
// ============================================================================

TEST_F(StatusBarPanelTest, RenderReturnsValidElement)
{
	StatusBarPanel panel(makeConfig(), base);
	auto el = panel.render(mockServer, "Terminal");
	ASSERT_TRUE(el);
}

TEST_F(StatusBarPanelTest, RendersServerStatusSegment)
{
	StatusBarPanel panel(makeConfig(), base);
	auto text = renderToString(panel.renderAt(mockServer, "Terminal", base));
	EXPECT_THAT(text, HasSubstr("Server Status"));
}

// ============================================================================
// Hint rotation (deterministic via renderAt)
// ============================================================================

TEST_F(StatusBarPanelTest, HintAtTimeZeroShowsFirstHint)
{
	StatusBarPanel panel(makeConfig(), base);
	auto text = renderToString(panel.renderAt(mockServer, "Terminal", base));
	EXPECT_THAT(text, HasSubstr("HINT_ZERO"));
}

TEST_F(StatusBarPanelTest, HintDoesNotAdvanceBeforeInterval)
{
	StatusBarPanel panel(makeConfig(), base);
	// Anywhere in [0, interval) stays on hints[0]; check just under interval.
	auto text =
		renderToString(panel.renderAt(mockServer, "Terminal", base + milliseconds(999)));
	EXPECT_THAT(text, HasSubstr("HINT_ZERO"));
}

TEST_F(StatusBarPanelTest, HintAdvancesAtInterval)
{
	StatusBarPanel panel(makeConfig(), base);
	auto text =
		renderToString(panel.renderAt(mockServer, "Terminal", base + milliseconds(1000)));
	EXPECT_THAT(text, HasSubstr("HINT_ONE"));
}

TEST_F(StatusBarPanelTest, HintWrapsAround)
{
	StatusBarPanel panel(makeConfig(), base);
	// 3 hints * 1000ms = full cycle; 3000ms wraps back to hints[0].
	auto text =
		renderToString(panel.renderAt(mockServer, "Terminal", base + milliseconds(3000)));
	EXPECT_THAT(text, HasSubstr("HINT_ZERO"));
}

// ============================================================================
// Per-tab keymaps
// ============================================================================

TEST_F(StatusBarPanelTest, ShowsKeymapForMatchingTab)
{
	StatusBarPanel panel(makeConfig(), base);
	auto text = renderToString(panel.renderAt(mockServer, "Terminal", base));
	EXPECT_THAT(text, HasSubstr("KEYMAP_TERMINAL"));
}

TEST_F(StatusBarPanelTest, OmitsKeymapForUnknownTabAndDoesNotThrow)
{
	StatusBarPanel panel(makeConfig(), base);
	std::string text;
	EXPECT_NO_THROW({
		text = renderToString(panel.renderAt(mockServer, "Nonexistent Tab", base));
	});
	EXPECT_THAT(text, Not(HasSubstr("KEYMAP_TERMINAL")));
}

// ============================================================================
// Empty config edge cases
// ============================================================================

TEST_F(StatusBarPanelTest, EmptyHintsAndKeymapsRendersServerStatusOnly)
{
	Config::StatusBarSettings empty;
	empty.hintIntervalMs = 1000; // valid; no hints
	StatusBarPanel panel(std::move(empty), base);

	std::string text;
	EXPECT_NO_THROW({
		text = renderToString(panel.renderAt(mockServer, "Terminal", base));
	});
	EXPECT_THAT(text, HasSubstr("Server Status"));
}
