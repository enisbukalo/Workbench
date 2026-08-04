#pragma once

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

/**
 * @file app.h
 * @brief Main application orchestrator for the Workbench TUI.
 *
 * Manages the application lifecycle and initializes the terminal UI
 * with all panels and the background monitoring system. The application
 * uses a singleton pattern with a static Run() entry point.
 *
 * The App class orchestrates the FTXUI layout, creating a grid structure
 * that contains:
 * - System resources panel (CPU/GPU/RAM monitoring)
 * - Model management panels (models list, presets, load settings)
 * - Inference settings panel (runtime parameters)
 * - Server status indicator
 * - Terminal panel with PTY support
 *
 * @note This class is intentionally minimal; all functionality is
 *       delegated to panel classes and the SystemMonitorRunner.
 *
 * @note The application uses a tabbed interface for the terminal
 *       functionality, supporting both static and dynamic terminal
 *       panels that can be spawned on demand.
 *
 * @note The application entry point is the static method App::run().
 */
class App {
  public:
	/**
	 * @brief Starts the main application loop.
	 *
	 * Creates an FTXUI fullscreen screen, initializes the background
	 * system monitor (CPU/GPU/RAM polling), and launches the main
	 * UI event loop with all panels arranged in a grid layout.
	 *
	 * This is the primary entry point for the application and blocks
	 * until the user exits the TUI.
	 */
	 static void run();

  private:
	/**
	 * @brief Default constructor - private to prevent instantiation.
	 *
	 * The App class uses only static methods and cannot be instantiated.
	 * This enforces the static-only design pattern where all functionality
	 * is accessed through App::run().
	 */
	App() = default;
};
