#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "llamaServerProcess.h"
#include "terminalPanel.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

/**
 * @file serverLogPanel.h
 * @brief Live log panel for displaying llama-server output.
 *
 * Uses a TerminalPanel internally to display the log file output in
 * real-time using native file watching (Get-Content -Wait on Windows,
 * tail -f on Linux). This provides proper auto-scrolling and real-time
 * updates without manual polling.
 */
class ServerLogPanel
{
  public:
	/**
	 * @brief Construct a ServerLogPanel.
	 *
	 * @param screen Reference to FTXUI screen for posting redraws.
	 */
	explicit ServerLogPanel(ftxui::ScreenInteractive &screen);

	/**
	 * @brief Destructor - shuts down the internal terminal.
	 */
	~ServerLogPanel();

	// Non-copyable
	ServerLogPanel(const ServerLogPanel &) = delete;
	ServerLogPanel &operator=(const ServerLogPanel &) = delete;

	/**
	 * @brief Get the FTXUI component for this panel.
	 * @return Component suitable for adding to a container.
	 */
	ftxui::Component component();

	/**
	 * @brief Start the log viewer (spawn the internal terminal).
	 */
	void start();

	/**
	 * @brief Stop the log viewer (shutdown the internal terminal).
	 */
	void stop();

	/**
	 * @brief Check if the log viewer is running.
	 */
	bool isRunning() const;

	/**
	 * @brief Clear the log display.
	 */
	void clear();

  private:
	// FTXUI references
	ftxui::ScreenInteractive &m_screen;

	// Internal terminal for displaying log output
	std::unique_ptr<TerminalPanel> m_terminal;

	// Log file path
	std::string m_logPath;

	// Component
	ftxui::Component m_component;
};
