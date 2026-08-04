/**
 * @file terminalPresetsPanel.cpp
 * @brief Terminal presets panel implementation.
 *
 * Implements a stateful FTXUI component that displays user-defined terminal
 * presets in a table with per-row remove buttons and an add-preset popup dialog.
 */

#include "terminalPresetsPanel.h"

#include "ThemeManager.h"
#include "ui_utils.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/table.hpp>

using namespace ftxui;

// =========================================================================
// Constructor
// =========================================================================

TerminalPresetsPanel::TerminalPresetsPanel(IConfigManager &config)
	: m_config(config), m_removeButtons(Container::Vertical({}))
{
	refreshPresets();
}

// =========================================================================
// Static helpers (pure logic, mock-tested)
// =========================================================================

bool TerminalPresetsPanel::addPreset(IConfigManager &config,
									 const std::string &name,
									 const std::string &command)
{
	if (name.empty() || command.empty())
		return false;

	Config::TerminalPreset preset;
	preset.name = name;
	preset.initialCommand = command;

	return config.addTerminalPreset(std::move(preset));
}

bool TerminalPresetsPanel::removePreset(IConfigManager &config,
										const std::string &name)
{
	return config.removeTerminalPreset(name);
}

// =========================================================================
// Instance methods
// =========================================================================

void TerminalPresetsPanel::refreshPresets()
{
	m_presets = m_config.getTerminalPresets();

	// Rebuild per-row remove buttons dynamically.
	m_removeButtons->DetachAllChildren();
	for (const auto &p : m_presets) {
		std::string name = p.name;
		auto theme = ThemeManager::instance().getActive();
		auto removeBtnStyle = ButtonOption::Animated();
		removeBtnStyle.transform = [theme](const EntryState &s) {
			auto e = text(s.label) | color(theme->deleteAction);
			if (s.focused)
				e |= bold;
			return e | center;
		};
		auto btn = Button(
			"[Remove]",
			[this, name] {
				removePreset(m_config, name);
				m_config.save();
				m_status = "Removed — restart to close tab";
				refreshPresets();
			},
			removeBtnStyle);
		m_removeButtons->Add(btn);
	}
}

void TerminalPresetsPanel::openAddPopup()
{
	m_newName.clear();
	m_newCommand.clear();
	m_addError.clear();
	m_showAddPopup = true;
}

void TerminalPresetsPanel::cancelAdd()
{
	m_showAddPopup = false;
}

void TerminalPresetsPanel::confirmAdd()
{
	if (m_newName.empty()) {
		m_addError = "Name required";
		return;
	}
	if (m_newCommand.empty()) {
		m_addError = "Command required";
		return;
	}

	if (!addPreset(m_config, m_newName, m_newCommand)) {
		m_addError = "Name in use";
		return;
	}

	if (!m_config.save()) {
		m_status = "Save failed";
	} else {
		m_status = "Saved — restart to open tab";
	}

	m_showAddPopup = false;
	refreshPresets();
}

// =========================================================================
// Component builder
// =========================================================================

Component TerminalPresetsPanel::component()
{
	auto themeOpt = InputOption();
	themeOpt.multiline = false;
	themeOpt.transform = [](InputState state) {
		auto theme = ThemeManager::instance().getActive();
		auto e = state.element | align_right;
		if (state.is_placeholder)
			return e | color(theme->toggleOff);
		return e | color(theme->toggleOn);
	};

	// Add-popup inputs + buttons.
	auto addNameInput = Input(&m_newName, "preset name", themeOpt);
	auto addCmdInput = Input(&m_newCommand, "command to run", themeOpt);

	auto btnStyle = ButtonOption::Animated();
	btnStyle.transform = [](const EntryState &s) {
		auto theme = ThemeManager::instance().getActive();
		auto e = text(s.label) | color(theme->label);
		if (s.focused)
			e |= bold;
		return e | center;
	};

	auto addSaveBtn = Button("Save", [this] { confirmAdd(); }, btnStyle);
	auto addCancelBtn = Button("Cancel", [this] { cancelAdd(); }, btnStyle);

	// Add-preset popup container.
	auto addPopupContainer = Container::Vertical({
		addNameInput,
		addCmdInput,
		Container::Horizontal({ addSaveBtn, addCancelBtn }),
	});

	auto addPopup = Renderer(addPopupContainer, [=, this] {
		auto theme = ThemeManager::instance().getActive();
		Elements rows;
		rows.push_back(
			ui_utils::settingRowComponent("Name", addNameInput->Render()));
		rows.push_back(
			ui_utils::settingRowComponent("Command", addCmdInput->Render()));
		if (!m_addError.empty())
			rows.push_back(text("  " + m_addError) | color(theme->error));
		rows.push_back(separatorLight());
		rows.push_back(hbox({
			filler(),
			addSaveBtn->Render(),
			separatorEmpty(),
			addCancelBtn->Render(),
		}));
		return window(text("Add Terminal Preset") | bold | color(theme->title),
					  vbox(std::move(rows))) |
			   size(WIDTH, GREATER_THAN, 50) | clear_under;
	});

	// "+ Add Preset" button below the table.
	auto addPresetBtnStyle = ButtonOption::Animated();
	addPresetBtnStyle.transform = [](const EntryState &s) {
		auto theme = ThemeManager::instance().getActive();
		auto e = text(s.label) | color(theme->success);
		if (s.focused)
			e |= bold;
		return e | center;
	};

	auto addPresetBtn =
		Button("+ Add Preset", [this] { openAddPopup(); }, addPresetBtnStyle);

	// Status line component.
	auto statusComponent = Renderer(Container::Vertical({}), [=, this] {
		auto theme = ThemeManager::instance().getActive();
		// v7 added a variadic vbox(Element) overload, making the empty
		// brace-init `vbox({})` ambiguous. Name the type explicitly.
		if (m_status.empty())
			return vbox(Elements{});
		Color statusColor =
			(m_status == "Save failed") ? theme->error : theme->success;
		return vbox({ text("  " + m_status) | color(statusColor) });
	});

	// Build child components for focus tree.
	auto contentContainer = Container::Horizontal({
		Container::Vertical({}), // placeholder — renderer draws table directly
		addPresetBtn,
		statusComponent,
		m_removeButtons,
	});

	// Main renderer: draws table + button + status each frame.
	m_component = Renderer(contentContainer, [=, this]() mutable {
		auto theme = ThemeManager::instance().getActive();

		// Header row.
		std::vector<Element> header = {
			text("Name") | bold,
			text("Command") | bold,
			text("Action") | bold,
		};

		// Data rows.
		std::vector<std::vector<Element>> rows;
		for (size_t i = 0; i < m_presets.size(); ++i) {
			const auto &preset = m_presets[i];
			bool isEven = (i % 2) == 0;
			Color nameColor = isEven ? theme->tableAltOdd : theme->tableAltEven;

			Element removeCell;
			if (static_cast<int>(i) < m_removeButtons->ChildCount()) {
				removeCell = m_removeButtons->ChildAt(i)->Render();
			} else {
				removeCell =
					text("[Remove]") | bold | color(theme->deleteAction);
			}

			rows.push_back({ text(preset.name) | color(nameColor),
							 text(preset.initialCommand) | color(nameColor),
							 removeCell });
		}

		// Insert header at beginning.
		rows.insert(rows.begin(), { header });

		Table table(rows);
		auto padding = [](Element e) { return hbox({ e, text(" ") }); };
		table.SelectAll().DecorateCells(padding);
		table.SelectRows(0, 1).DecorateCells(bgcolor(theme->tableHeaderBg));
		table.SelectRows(0, 1).DecorateCells(color(theme->tableHeaderText));

		auto tableWindow =
			window(text("Terminal Presets") | bold | color(theme->title),
				   hbox({ text("    "), vbox({ table.Render() }) }),
				   ftxui::EMPTY);

		return vbox({
			tableWindow,
			addPresetBtn->Render(),
			statusComponent->Render(),
			filler(),
		});
	});

	// Overlay the add-preset popup.
	m_component = Modal(m_component, addPopup, &m_showAddPopup);

	// Esc closes popup while open.
	m_component = m_component | CatchEvent([this](Event event) {
					  if (m_showAddPopup && event == Event::Escape) {
						  cancelAdd();
						  return true;
					  }
					  return false;
				  });

	return m_component;
}
