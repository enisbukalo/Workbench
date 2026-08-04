#pragma once
#include <algorithm>
#include <string>

/**
 * @file userSettings.h
 * @brief Terminal emulator configuration settings.
 *
 * This header defines the TerminalSettings structure which controls
 * the embedded terminal emulator's behavior and initialization.
 * Users can create and save multiple named terminal configurations.
 */

namespace Config {

/**
 * @brief Terminal preset configuration.
 *
 * A named terminal configuration that users can create, edit, and delete
 * via the UI. Each preset appears as a dynamic top-level tab in the
 * application window.
 *
 * @note Working directory defaults to the application's current working
 *       directory if not specified.
 * @note The name is used as the tab display name and must be unique.
 *
 * @code
 * // Create an opencode terminal preset
 * TerminalPreset opencodePreset;
 * opencodePreset.name = "Opencode";
 * opencodePreset.initialCommand = "opencode";
 *
 * // Create a gitui terminal preset
 * TerminalPreset gituiPreset;
 * gituiPreset.name = "GitUI";
 * gituiPreset.initialCommand = "gitui";
 * gituiPreset.cols = 120;
 * gituiPreset.rows = 40;
 * @endcode
 */
struct TerminalPreset
{
	/**
	 * @brief Display name for the terminal tab.
	 *
	 * The name shown in the tab bar. Must be unique across all presets.
	 *
	 * @note Examples: "Opencode", "GitUI", "Neovim", "Dev Terminal"
	 */
	std::string name;

	/**
	 * @brief Command to execute when terminal opens.
	 *
	 * The program or command that runs when this terminal tab is created.
	 *
	 * @note Examples: "opencode", "gitui", "nvim", "htop"
	 */
	std::string initialCommand;

	/**
	 * @brief Terminal width in columns.
	 *
	 * @default 80
	 */
	int cols = 80;

	/**
	 * @brief Terminal height in rows.
	 *
	 * @default 24
	 */
	int rows = 24;

	/**
	 * @brief Clamp fields; set name to "Unnamed" if empty.
	 *
	 * Not noexcept — string assignment may throw std::bad_alloc.
	 */
	void validate();
};

/**
 * @brief Terminal emulator settings.
 *
 * Configuration for a named terminal instance. Users can create
 * multiple terminal settings and save them, allowing quick switching
 * between different terminal configurations (e.g., dev terminal,
 * root terminal, project-specific terminals).
 *
 * @note Built-in terminals like "gitui" and "opencode" are handled
 *       in code, not stored in config.
 * @note Users create and save custom terminal configurations via UI.
 *
 * @code
 * // Create a development terminal
 * TerminalSettings dev_terminal;
 * dev_terminal.name = "dev";
 * dev_terminal.defaultShell = "/bin/bash";
 * dev_terminal.workingDirectory = "/home/user/projects";
 * dev_terminal.initialCommand = "git status";
 *
 * // Create a root terminal
 * TerminalSettings root_terminal;
 * root_terminal.defaultShell = "/bin/bash";
 * root_terminal.initialCommand = "sudo bash";
 * @endcode
 */
struct TerminalSettings
{
	/**
	 * @brief Name identifier for this terminal configuration.
	 *
	 * Used to reference this terminal in the UI and config. Should be
	 * unique across all user-created terminals.
	 *
	 * @note Built-in terminals ("gitui", "opencode") are not stored
	 *       here - they're created in code.
	 * @example "dev", "root", "project-x", "debug"
	 */
	std::string name;

	/**
	 * @brief Shell program to execute.
	 *
	 * The shell executable to use. If empty, uses the system default:
	 * - Windows: cmd.exe or PowerShell
	 * - Linux/macOS: /bin/bash or user's configured shell
	 *
	 * @default "" (system default)
	 * @note Examples: "/bin/bash", "/bin/zsh", "powershell.exe"
	 * @see std::system for shell execution details
	 */
	std::string defaultShell;

	/**
	 * @brief Command to execute after shell starts.
	 *
	 * A command that runs automatically when the terminal opens.
	 * Useful for setting up environment or running diagnostics.
	 *
	 * @default "" (none)
	 * @note The command runs in the context of defaultShell.
	 * @note Examples: "cd /project && git status", "nvim"
	 */
	std::string initialCommand;

	/**
	 * @brief Working directory for the shell.
	 *
	 * The directory the shell starts in. If empty or invalid,
	 * uses the application's current working directory.
	 *
	 * @default "" (current directory)
	 * @note Should be an absolute path for reliability.
	 * @note The directory must exist and be accessible.
	 */
	std::string workingDirectory;

	/**
	 * @brief Default terminal width in columns.
	 *
	 * Fallback column count when the terminal size cannot be
	 * determined or for initial allocation.
	 *
	 * @default 80
	 * @range 16 to maximum terminal width
	 * @note Modern terminals typically support 120+ columns.
	 */
	int defaultCols = 80;

	/**
	 * @brief Default terminal height in rows.
	 *
	 * Fallback row count when the terminal size cannot be
	 * determined or for initial allocation.
	 *
	 * @default 24
	 * @range 8 to maximum terminal height
	 * @note Combined with defaultCols, determines initial buffer size.
	 */
	int defaultRows = 24;

	/**
	 * @brief Clamp fields; set name to "Unnamed" if empty.
	 *
	 * Not noexcept — string assignment may throw std::bad_alloc.
	 */
	void validate();
};

} // namespace Config
