/**
 * @file terminalPanel.h
 * @brief Stateful terminal panel that embeds a PTY + VTerm in an FTXUI tab.
 *
 * This class implements a terminal emulator that integrates three components:
 * 1. PtyHandler - Platform-specific PTY management (ConPTY on Windows, forkpty
 * on Linux)
 * 2. libvterm - ANSI escape sequence parser for terminal content
 * 3. FTXUI - UI rendering framework for the terminal display
 *
 * The terminal supports:
 * - Dynamic spawning and shutdown of shell processes
 * - ANSI color and attribute rendering
 * - Terminal resize handling
 * - Input capture mode (Ctrl+T to toggle)
 * - UTF-8 character encoding
 *
 * Thread model:
 * - Main thread: Handles rendering and event processing
 * - Background thread (ReadLoop): Reads PTY output and posts redraw events
 * - Thread-safe access to vterm state via m_vtermMutex
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include "ptyHandler.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

struct VTerm;
struct VTermScreen;

class TerminalPanel
{
  public:
	/**
	 * @brief Constructs a TerminalPanel attached to the given FTXUI screen.
	 *
	 * @param screen Reference to the FTXUI interactive screen that will
	 *               render this terminal panel.
	 * @param initialCommand Optional command to execute after the shell
	 *                        produces its first output (e.g., "cd /home/user").
	 * @note The terminal is not spawned until Spawn() is called explicitly.
	 *       The component must be added to the UI before spawning to
	 *       receive proper dimensions.
	 */
	explicit TerminalPanel(ftxui::ScreenInteractive &screen,
						   std::string initial_command = "");

	/**
	 * @brief Destructor that safely shuts down the terminal.
	 *
	 * This method:
	 * 1. Signals the ReadLoop thread to stop
	 * 2. Joins the ReadLoop thread
	 * 3. Closes the PTY and terminates the shell process
	 * 4. Frees the vterm structures
	 *
	 * @note Safe to call multiple times; subsequent calls are no-ops.
	 * @note Must be called before the TerminalPanel object is destroyed.
	 */
	~TerminalPanel();

	/**
	 * @brief Deleted copy constructor — terminal state cannot be duplicated.
	 *
	 * Each terminal panel owns a unique PTY and vterm instance that
	 * cannot be shared or copied.
	 */
	TerminalPanel(const TerminalPanel &) = delete;

	/**
	 * @brief Deleted copy-assignment operator — terminal state cannot be
	 * duplicated.
	 *
	 * Each terminal panel owns a unique PTY and vterm instance that
	 * cannot be shared or copied.
	 */
	TerminalPanel &operator=(const TerminalPanel &) = delete;

	/**
	 * @brief Creates an FTXUI Component that wraps the terminal panel.
	 *
	 * This method returns a Component that can be used in FTXUI's
	 * component hierarchy. The component:
	 * - Renders the terminal screen via RenderScreen()
	 * - Captures keyboard events via HandleEvent()
	 * - Reflects its bounding box via the reflect() modifier
	 *
	 * @return An ftxui::Component that can be added to a container.
	 * @note The component must be spawned (via spawn()) before it
	 *       will render actual terminal content.
	 */
	ftxui::Component component();

	/**
	 * @brief Spawns a shell process and initializes the terminal.
	 *
	 * This method:
	 * 1. Creates a vterm instance with current dimensions
	 * 2. Initializes the vterm screen with UTF-8 support
	 * 3. Spawns a shell via PtyHandler
	 * 4. Starts the ReadLoop background thread
	 *
	 * Dimensions are taken from the reflected box if available,
	 * otherwise defaults to 80x24.
	 *
	 * @note If spawn() is called multiple times, only the first call
	 *       has any effect; subsequent calls are no-ops.
	 * @note The initial command (if provided) is sent after the shell
	 *       produces its first output, ensuring the prompt is ready.
	 */
	void spawn();

	/**
	 * @brief Shuts down the terminal and releases all resources.
	 *
	 * This method:
	 * 1. Signals the ReadLoop thread to stop via m_stopFlag
	 * 2. Notifies m_cv to wake the thread if it's waiting
	 * 3. Joins the ReadLoop thread
	 * 4. Closes the PTY and terminates the shell
	 * 5. Frees the vterm structures
	 *
	 * @note Safe to call multiple times; subsequent calls are no-ops.
	 * @see spawn()
	 */
	void shutdown();

	/**
	 * @brief Checks whether the terminal has been spawned.
	 *
	 * @return true if spawn() has been called and the shell is running,
	 *         false otherwise.
	 */
	bool isSpawned() const;

	/**
	 * @brief Determines whether the terminal should intercept keyboard events.
	 *
	 * This method is called by the parent container to determine if the
	 * terminal should handle an incoming event. The terminal captures
	 * events when:
	 * - It has been spawned
	 * - The shell is still running (m_ptyDead is false)
	 * - Capture mode is enabled (m_capturing is true)
	 * - The event is character input or a special key
	 *
	 * @param event The event to check.
	 * @return true if the terminal should handle this event, false otherwise.
	 * @see handleEvent()
	 */
	bool wantsEvent(ftxui::Event event) const;

	/**
	 * @brief Checks whether the terminal is in capture mode.
	 *
	 * In capture mode, the terminal intercepts all keyboard input.
	 * When not capturing, input is passed to the FTXUI container for
	 * tab navigation and other UI interactions.
	 *
	 * @return true if the terminal is capturing input, false otherwise.
	 * @see setCapturing()
	 */
	bool isCapturing() const;

	/**
	 * @brief Sets the capture mode state.
	 *
	 * @param value true to capture all keyboard input, false to pass
	 *              input to the parent container.
	 * @note Toggling between capture modes can be done via Ctrl+T
	 *       from the terminal itself.
	 * @see isCapturing()
	 */
	void setCapturing(bool value);

	/**
	 * @brief Sends Ctrl+C (ETX byte \x03) to the PTY.
	 *
	 * Writing \x03 to the master side of the PTY causes the slave's
	 * line discipline to generate SIGINT for the foreground process
	 * group, exactly as if the user typed Ctrl+C in a real terminal.
	 *
	 * @note Thread-safe; uses the PTY's internal mutex.
	 */
	void sendCtrlC();

	/**
	 * @brief Handles an FTXUI event and dispatches it to the PTY.
	 *
	 * This method processes various types of events:
	 * - Special keys (Enter, Tab, Arrow keys, etc.) are converted to
	 *   vterm keyboard events
	 * - Character input is UTF-8 decoded and sent to vterm
	 * - Ctrl+T toggles capture mode
	 * - Other input is passed directly to the PTY as raw bytes
	 *
	 * @param event The event to handle.
	 * @return true if the event was handled by the terminal, false
	 *         otherwise.
	 * @note This method assumes the terminal is spawned and the shell
	 *       is still running.
	 */
	bool handleEvent(ftxui::Event event);

	friend void Vterm_output_cb(const char *s, size_t len, void *user);

  private:
	/**
	 * @brief Background thread function that reads PTY output and updates vterm.
	 *
	 * This loop:
	 * 1. Reads available data from the PTY master fd
	 * 2. Feeds the data to vterm_input_write() for parsing
	 * 3. Posts a Custom event to trigger a redraw
	 * 4. Sends the initial command after the first shell output
	 * 5. Detects shell exit via m_pty.isAlive()
	 *
	 * The loop terminates when m_stopFlag is set, either by calling
	 * Shutdown() or when the shell process exits.
	 *
	 * @note This method is called by the m_readThread member.
	 * @note Thread-safe access to vterm state via m_vtermMutex.
	 */
	void ReadLoop();

	/**
	 * @brief Resizes the vterm and PTY to new dimensions.
	 *
	 * This method must be called with m_vtermMutex held. It:
	 * 1. Calls vterm_set_size() to update the vterm dimensions
	 * 2. Calls m_pty.resize() to notify the shell of the new size
	 * 3. Updates m_cols and m_rows member variables
	 *
	 * @param new_cols The new column count (width).
	 * @param new_rows The new row count (height).
	 * @note This method is called from renderScreen() when the bounding
	 *       box dimensions change.
	 */
	void resize(int new_cols, int new_rows);

	/**
	 * @brief Renders the vterm screen as FTXUI elements.
	 *
	 * This method:
	 * 1. Checks if the terminal is spawned and the shell is alive
	 * 2. Detects dimension changes and calls resize() if needed
	 * 3. Iterates over all cells in the vterm screen
	 * 4. Converts each cell's character and attributes to FTXUI elements
	 * 5. Applies colors, bold, underline, and other attributes
	 * 6. Wraps the result in a window element
	 *
	 * @return An ftxui::Element representing the terminal screen.
	 * @note This method is thread-safe; m_vtermMutex must be held by the caller.
	 * @see renderLoop()
	 */
	ftxui::Element renderScreen();

	// Member variables (renamed to m_ prefix per project conventions)
	ftxui::ScreenInteractive &m_screen;
	VTerm *m_vt = nullptr;
	VTermScreen *m_vts = nullptr;
	int m_rows = 24;
	int m_cols = 80;
	std::atomic<bool> m_stopFlag{ false };
	std::atomic<bool> m_spawned{ false };
	std::atomic<bool> m_ptyDead{ false };
	std::mutex m_vtermMutex;
	std::mutex m_cvMutex;
	std::condition_variable m_cv;
	std::thread m_readThread;
	ftxui::Box m_box = {};
	std::string m_initialCommand;
	std::atomic<bool> m_initialCmdSent{ false };
	std::atomic<bool> m_capturing{ true };
	PtyHandler m_pty;
};
