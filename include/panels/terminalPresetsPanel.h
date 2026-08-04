#pragma once

#include "IConfigManager.h"

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <string>
#include <string_view>
#include <vector>

/**
 * @file terminalPresetsPanel.h
 * @brief Interactive panel for managing user-defined terminal presets.
 *
 * Provides a stateful FTXUI component with preset table, add-preset popup
 * dialog, and per-row remove buttons. Presets are persisted via IConfigManager;
 * new tabs spawn on next application restart.
 */
class TerminalPresetsPanel
{
  public:
	/**
	 * @brief Construct the panel backed by a config manager.
	 * @param config ConfigManager reference for preset CRUD + persistence.
	 */
	explicit TerminalPresetsPanel(IConfigManager &config);

	/**
	 * @brief Returns the interactive FTXUI component.
	 * @return Component wrapping the preset table, add button, and popup modal.
	 */
	[[nodiscard]] ftxui::Component component();

	// Popup flow — public for testability (same tradeoff as ModelsPanel).
	void openAddPopup();
	void cancelAdd();
	void confirmAdd();
	[[nodiscard]] std::string_view getNewName() const
	{
		return m_newName;
	}
	[[nodiscard]] std::string_view getNewCommand() const
	{
		return m_newCommand;
	}
	void setNewName(const std::string &name)
	{
		m_newName = name;
	}
	void setNewCommand(const std::string &cmd)
	{
		m_newCommand = cmd;
	}

	// Kept static: pure logic, exercised by existing unit tests.

	/**
	 * @brief Adds a new terminal preset.
	 * @param config ConfigManager reference for persistence.
	 * @param name Display name for the tab (e.g., "Opencode", "GitUI")
	 * @param command Command to execute (e.g., "opencode", "gitui")
	 * @return true if preset was added, false if name already exists or empty
	 */
	static bool addPreset(IConfigManager &config,
						  const std::string &name,
						  const std::string &command);

	/**
	 * @brief Removes an existing terminal preset by name.
	 * @param config ConfigManager reference for persistence.
	 * @param name Name of the preset to remove
	 * @return true if preset was found and removed, false otherwise
	 */
	static bool removePreset(IConfigManager &config, const std::string &name);

  private:
	void refreshPresets();

	IConfigManager &m_config;
	std::vector<Config::TerminalPreset> m_presets;
	std::string m_newName;
	std::string m_newCommand;
	std::string m_addError;
	std::string m_status;
	bool m_showAddPopup = false;
	ftxui::Component m_removeButtons;
	ftxui::Component m_component;
};
