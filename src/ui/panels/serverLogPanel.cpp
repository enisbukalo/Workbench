/**
 * @file serverLogPanel.cpp
 * @brief Implementation of live log panel using TerminalPanel.
 *
 * Uses TerminalPanel internally to watch the llama-server log file
 * in real-time using native file watching commands.
 */

#include "serverLogPanel.h"

#include <spdlog/spdlog.h>

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace ftxui;

// ---------------------------------------------------------------------------
// Platform-specific log watching command
// ---------------------------------------------------------------------------

static std::string getLogWatchCommand(const std::string &logPath)
{
#ifdef _WIN32
	// Windows: cls to clear screen, then Get-Content with -Wait for real-time
	// streaming
	return "cls; Get-Content -Path \"" + logPath + "\" -Wait";
#else
	// Linux: clear to clear screen, then tail -f for real-time streaming
	return "clear; tail -f \"" + logPath + "\"";
#endif
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

ServerLogPanel::ServerLogPanel(ftxui::ScreenInteractive &screen)
	: m_screen(screen), m_logPath(LlamaServerProcess::getLogPath())
{
	spdlog::debug("ServerLogPanel: log path = {}", m_logPath);
}

ServerLogPanel::~ServerLogPanel()
{
	stop();
}

// ---------------------------------------------------------------------------
// Component creation
// ---------------------------------------------------------------------------

Component ServerLogPanel::component()
{
	if (m_component == nullptr) {
		// Create the internal terminal with the log-watching command
		std::string watchCmd = getLogWatchCommand(m_logPath);
		spdlog::debug("ServerLogPanel: watch command = {}", watchCmd);

		m_terminal = std::make_unique<TerminalPanel>(m_screen, watchCmd);
		m_terminal->setCapturing(false);

		// Create component that wraps the terminal
		m_component = m_terminal->component();

		// Auto-start the log viewer
		start();
	}

	return m_component;
}

// ---------------------------------------------------------------------------
// Start/Stop
// ---------------------------------------------------------------------------

void ServerLogPanel::start()
{
	if (m_terminal && !m_terminal->isSpawned()) {
		spdlog::info("ServerLogPanel: starting log viewer");
		m_terminal->spawn();
	}
}

void ServerLogPanel::stop()
{
	if (m_terminal && m_terminal->isSpawned()) {
		spdlog::info("ServerLogPanel: stopping log viewer");
		m_terminal->shutdown();
	}
}

bool ServerLogPanel::isRunning() const
{
	return m_terminal && m_terminal->isSpawned();
}

// ---------------------------------------------------------------------------
// Public methods
// ---------------------------------------------------------------------------

void ServerLogPanel::clear()
{
	// The terminal handles its own display; no need to clear anything here.
	// If needed, we could restart the terminal to clear its buffer.
	spdlog::debug("ServerLogPanel: clear requested (no-op)");
}
