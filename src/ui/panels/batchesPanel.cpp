#include "batchesPanel.h"

#include "ThemeManager.h"
#include "ui_utils.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/dom/elements.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>

using namespace ftxui;

BatchesPanel::BatchesPanel(AppDependencies &deps)
	: m_batches(deps.batches), m_modelsIni(deps.modelsIni),
	  m_server(deps.server), m_tracker(deps.tracker),
	  m_batchTracker(deps.batchTracker)
{
	refreshBatchNames();
	refreshPresetNames();
	initEditor();
}

// =========================================================================
// Data refresh
// =========================================================================

void BatchesPanel::refreshBatchNames()
{
	m_batchNames = m_batches.getBatchNames();
	if (m_batchNames.empty())
		m_selectedBatchIndex = -1;
	else if (m_selectedBatchIndex < 0 ||
			 m_selectedBatchIndex >= static_cast<int>(m_batchNames.size()))
		// Default to the first batch (and clamp an out-of-range selection back
		// in) so the editor populates on construction. The FTXUI Menu likewise
		// starts at index 0 when there is at least one entry.
		m_selectedBatchIndex = 0;
}

bool BatchesPanel::refreshPresetNames()
{
	auto next = m_modelsIni.getModelNames();
	if (next == m_allPresetNames)
		return false;
	m_allPresetNames = std::move(next);
	return true;
}

void BatchesPanel::initEditor()
{
	// Populate the editor up-front so the panel is usable before any Menu
	// on_change fires (which never happens for the initial/single-batch case).
	if (m_selectedBatchIndex >= 0) {
		loadSelectedIntoEditor();
		m_lastBatchIndex = m_selectedBatchIndex;
	} else {
		startNewBatch();
	}
}

void BatchesPanel::onShown()
{
	// The Model Settings panel may have added/removed presets in the shared
	// models.ini while this panel was hidden. Re-pull and, if the set changed,
	// rebuild the slot dropdowns so new presets become selectable (and removed
	// ones drop out). Existing picks that still exist are preserved by
	// rebuildSlotContainer's find-by-name.
	if (refreshPresetNames()) {
		// A preset may have vanished; drop any editor pick no longer valid.
		for (auto &pick : m_editPresets) {
			if (std::find(m_allPresetNames.begin(),
						  m_allPresetNames.end(),
						  pick) == m_allPresetNames.end())
				pick = firstUnusedPreset(-1);
		}
		// Honor the distinct-preset cap if presets shrank.
		setPresetCount(m_editCount);
		rebuildSlotContainer();
	}
}

std::string BatchesPanel::selectedBatchName() const
{
	if (m_selectedBatchIndex >= 0 &&
		m_selectedBatchIndex < static_cast<int>(m_batchNames.size()))
		return m_batchNames[m_selectedBatchIndex];
	return "";
}

// =========================================================================
// Editor state
// =========================================================================

void BatchesPanel::startNewBatch()
{
	m_editName.clear();
	m_status.clear();
	m_editPresets.clear();
	setPresetCount(MIN_COUNT);
}

void BatchesPanel::loadSelectedIntoEditor()
{
	const std::string name = selectedBatchName();
	if (name.empty())
		return;
	const auto batch = m_batches.getBatch(name);
	if (!batch.has_value())
		return;
	m_editName = batch->name;
	m_editPresets = batch->presets;
	m_status.clear();
	// Count follows the stored batch size, clamped to the editor range.
	setPresetCount(static_cast<int>(m_editPresets.size()));
}

int BatchesPanel::maxCount() const
{
	return std::min(MAX_COUNT, static_cast<int>(m_allPresetNames.size()));
}

std::string BatchesPanel::firstUnusedPreset(int exceptRow) const
{
	for (const auto &name : m_allPresetNames) {
		bool used = false;
		for (std::size_t r = 0; r < m_editPresets.size(); ++r) {
			if (static_cast<int>(r) == exceptRow)
				continue;
			if (m_editPresets[r] == name) {
				used = true;
				break;
			}
		}
		if (!used)
			return name;
	}
	return ""; // all presets already used
}

void BatchesPanel::setPresetCount(int count)
{
	// Cap at the number of distinct presets — a batch cannot hold a preset
	// twice, so it can never have more rows than there are presets.
	const int hi = std::max(MIN_COUNT, maxCount());
	const int clamped = std::clamp(count, MIN_COUNT, hi);
	m_editCount = clamped;
	m_editCountStr = std::to_string(clamped);

	if (static_cast<int>(m_editPresets.size()) < clamped) {
		// Grow: append rows at the bottom, each defaulting to the first preset
		// not already used by another row (keeps every row distinct).
		while (static_cast<int>(m_editPresets.size()) < clamped)
			m_editPresets.push_back(firstUnusedPreset(-1));
	} else if (static_cast<int>(m_editPresets.size()) > clamped) {
		// Shrink: drop from the bottom only.
		m_editPresets.resize(clamped);
	}
	rebuildSlotContainer();
}

void BatchesPanel::rebuildSlotContainer()
{
	// Rebuild per-slot dropdowns + backing storage in place. The container is
	// stable so it stays in the interactive tree; only its children are detached
	// and re-added, keeping event routing on the live dropdowns.
	if (!m_slotContainer)
		m_slotContainer = Container::Vertical({});
	m_slotContainer->DetachAllChildren();
	m_slotIdx.clear();
	m_slotOptions.clear();

	for (std::size_t i = 0; i < m_editPresets.size(); ++i) {
		const int slotPos = static_cast<int>(i);

		// This row's options = all presets EXCEPT those chosen in OTHER rows, so
		// no preset can be picked twice. This row's own current pick stays in.
		auto opts = std::make_unique<std::vector<std::string>>();
		for (const auto &name : m_allPresetNames) {
			bool usedElsewhere = false;
			for (std::size_t r = 0; r < m_editPresets.size(); ++r) {
				if (static_cast<int>(r) == slotPos)
					continue;
				if (m_editPresets[r] == name) {
					usedElsewhere = true;
					break;
				}
			}
			if (!usedElsewhere)
				opts->push_back(name);
		}
		std::vector<std::string> *optsPtr = opts.get();
		m_slotOptions.push_back(std::move(opts));

		// Index of this row's current pick within its filtered option list.
		auto idx = std::make_unique<int>(0);
		const auto it =
			std::find(optsPtr->begin(), optsPtr->end(), m_editPresets[i]);
		if (it != optsPtr->end())
			*idx = static_cast<int>(it - optsPtr->begin());
		int *idxPtr = idx.get();
		m_slotIdx.push_back(std::move(idx));

		auto onChange = [this, idxPtr, optsPtr, slotPos] {
			if (slotPos < static_cast<int>(m_editPresets.size()) &&
				*idxPtr >= 0 && *idxPtr < static_cast<int>(optsPtr->size())) {
				m_editPresets[slotPos] = (*optsPtr)[*idxPtr];
				// Defer the re-prune: rebuilding here would detach/free the very
				// dropdown whose event is running. The renderer picks this up
				// next frame.
				m_slotsDirty = true;
			}
		};
		m_slotContainer->Add(ui_utils::makeDropdown(optsPtr, idxPtr, onChange));
	}
}

// =========================================================================
// Persistence actions
// =========================================================================

void BatchesPanel::saveEditedBatch()
{
	if (m_editName.empty()) {
		m_status = "Batch name required";
		return;
	}
	if (m_allPresetNames.empty()) {
		m_status = "No presets defined";
		return;
	}
	if (m_batches.saveBatch({ m_editName, m_editPresets })) {
		m_status = "Saved batch '" + m_editName + "'";
		refreshBatchNames();
		for (std::size_t i = 0; i < m_batchNames.size(); ++i) {
			if (m_batchNames[i] == m_editName) {
				m_selectedBatchIndex = static_cast<int>(i);
				break;
			}
		}
	} else {
		m_status = "Failed to save batch";
	}
}

void BatchesPanel::deleteSelectedBatch()
{
	const std::string name = selectedBatchName();
	if (name.empty()) {
		m_status = "No batch selected";
		return;
	}
	if (m_batches.deleteBatch(name)) {
		m_status = "Deleted batch '" + name + "'";
		refreshBatchNames();
	} else {
		m_status = "Failed to delete batch";
	}
}

// =========================================================================
// Load / unload actions
// =========================================================================

void BatchesPanel::loadSelectedBatch()
{
	const std::string name = selectedBatchName();
	if (name.empty()) {
		m_status = "No batch selected";
		return;
	}
	if (!m_server.isRunning()) {
		m_status = "Start the server first";
		return;
	}
	const auto batch = m_batches.getBatch(name);
	if (!batch.has_value())
		return;

	const auto known = m_modelsIni.getModelNames();
	const auto models = m_tracker.snapshot();
	int loaded = 0;
	int skipped = 0;
	for (const auto &section : batch->presets) {
		if (std::find(known.begin(), known.end(), section) == known.end()) {
			spdlog::warn("Batch '{}' references unknown preset '{}', skipping",
						 name,
						 section);
			++skipped;
			continue;
		}
		// A preset is unique server-side and can't be loaded twice — skip any
		// already LOADED (whether by this batch, another batch, or a single
		// click). The model panel/info already reflect it.
		const auto it = models.find(section);
		if (it != models.end() &&
			it->second.lifecycle == ModelLifecycle::LOADED) {
			++skipped;
			continue;
		}
		// Mirror the control-API load path: record intent then fire the load;
		// the monitor confirms LOADED on its next poll (no per-preset UI poll).
		m_tracker.requestLoad(section);
		if (m_server.loadModel(section))
			++loaded;
		else
			spdlog::error("Batch '{}': loadModel('{}') failed", name, section);
	}
	m_status = "Loaded " + std::to_string(loaded) + " preset(s)" +
			   (skipped ? ", skipped " + std::to_string(skipped) : "");
}

void BatchesPanel::unloadSelectedBatch()
{
	const std::string name = selectedBatchName();
	if (name.empty()) {
		m_status = "No batch selected";
		return;
	}
	const auto batch = m_batches.getBatch(name);
	if (!batch.has_value())
		return;

	int unloaded = 0;
	for (const auto &section : batch->presets) {
		// Per-section unload — the batch may be a subset of what is loaded, so
		// never requestUnloadAll here.
		m_tracker.requestUnload(section);
		if (m_server.unloadModel(section))
			++unloaded;
	}
	m_status = "Unloaded " + std::to_string(unloaded) + " preset(s)";
}

void BatchesPanel::onLoadUnloadClicked()
{
	if (m_loadLabel == "UNLOAD")
		unloadSelectedBatch();
	else
		// "LOAD" and "Partially Loaded" both load — loadSelectedBatch already
		// skips presets already LOADED, so a partial batch is topped up.
		loadSelectedBatch();
	updateLoadLabel();
}

void BatchesPanel::updateLoadLabel()
{
	const std::string name = selectedBatchName();
	if (name.empty()) {
		m_loadLabel = "LOAD";
		return;
	}
	const auto batch = m_batches.getBatch(name);
	if (!batch.has_value()) {
		m_loadLabel = "LOAD";
		return;
	}
	// Derive batch status from per-preset truth (ModelStateTracker snapshot).
	switch (m_batchTracker.statusOf(batch->presets, m_tracker.snapshot())) {
	case BatchLifecycle::LOADED:
		m_loadLabel = "UNLOAD";
		break;
	case BatchLifecycle::LOADING:
		// Steady partial state (e.g. some presets unloaded manually), not an
		// in-flight load — read as a state, and clicking tops up the rest.
		m_loadLabel = "Partially Loaded";
		break;
	case BatchLifecycle::UNLOADED:
	default:
		m_loadLabel = "LOAD";
		break;
	}
}

// =========================================================================
// Component
// =========================================================================

ftxui::Component BatchesPanel::component()
{
	if (m_component)
		return m_component;

	rebuildSlotContainer();

	// Themed input option (avoids the default inverted/white focus box).
	InputOption inputOpt;
	inputOpt.multiline = false;
	inputOpt.transform = [](InputState state) {
		auto theme = ThemeManager::instance().getActive();
		auto e = state.element;
		if (state.is_placeholder)
			return e | color(theme->toggleOff);
		return e | color(theme->toggleOn);
	};

	auto btnStyle = ButtonOption::Animated();
	btnStyle.transform = [](const EntryState &s) {
		auto t = ThemeManager::instance().getActive();
		auto e = text(s.label) | color(t->label);
		if (s.focused)
			e |= bold;
		return e | center;
	};

	// Bottom LOAD/UNLOAD button — colors mirror ModelsPanel's startStopBtnStyle.
	auto loadBtnStyle = ButtonOption::Animated();
	loadBtnStyle.transform = [](const EntryState &s) {
		auto t = ThemeManager::instance().getActive();
		Color textColor;
		if (s.label == "Partially Loaded")
			textColor = t->warning;
		else if (s.label == "LOAD")
			textColor = t->success;
		else
			textColor = t->error; // UNLOAD
		auto e = text(s.label) | color(textColor);
		if (s.focused)
			e |= bold;
		return e | center;
	};

	// Left: batch list.
	MenuOption batchMenuOpt;
	batchMenuOpt.on_change = [this] { loadSelectedIntoEditor(); };
	batchMenuOpt.entries_option.transform = [](const EntryState &s) {
		auto t = ThemeManager::instance().getActive();
		auto e = text(s.label);
		e |= color(s.active ? t->toggleOn : t->toggleOff);
		if (s.focused)
			e |= bold;
		return e;
	};
	auto batchMenu = Menu(&m_batchNames, &m_selectedBatchIndex, batchMenuOpt);

	auto nameInput = Input(&m_editName, "batch name", inputOpt);

	// Count [-] [input] [+] (1–10).
	InputOption countOpt = inputOpt;
	countOpt.transform = [](InputState state) {
		auto theme = ThemeManager::instance().getActive();
		auto e = state.element | center;
		if (state.is_placeholder)
			return e | color(theme->toggleOff);
		return e | color(theme->toggleOn);
	};
	countOpt.on_change = [this] {
		try {
			setPresetCount(std::stoi(m_editCountStr));
		} catch (...) {
		}
	};
	auto countInput = Input(&m_editCountStr, "1", countOpt);
	auto countMinus =
		Button("-", [this] { setPresetCount(m_editCount - 1); }, btnStyle);
	auto countPlus =
		Button("+", [this] { setPresetCount(m_editCount + 1); }, btnStyle);

	// Save / Delete (centered, kept at their position).
	auto saveBtn = Button("Save", [this] { saveEditedBatch(); }, btnStyle);
	auto deleteBtn =
		Button("Delete", [this] { deleteSelectedBatch(); }, btnStyle);
	auto newBtn = Button("New", [this] { startNewBatch(); }, btnStyle);

	// Single bottom LOAD/UNLOAD button.
	auto loadBtn =
		Button(&m_loadLabel, [this] { onLoadUnloadClicked(); }, loadBtnStyle);

	auto editorControls = Container::Vertical({
		nameInput,
		Container::Horizontal({ countMinus, countInput, countPlus }),
		m_slotContainer,
		Container::Horizontal({ newBtn, saveBtn, deleteBtn }),
		loadBtn,
	});

	auto root = Container::Horizontal({ batchMenu, editorControls });

	m_component = Renderer(root, [=, this] {
		auto t = ThemeManager::instance().getActive();

		// Selection-change populate. MenuOption::on_change only fires when the
		// index changes, so the initial/single-batch case never triggers it;
		// this render-loop check is the reliable path.
		if (m_selectedBatchIndex != m_lastBatchIndex) {
			m_lastBatchIndex = m_selectedBatchIndex;
			loadSelectedIntoEditor();
		}

		updateLoadLabel();

		// A slot pick changed last frame — re-prune the dropdowns now (safe
		// here, outside the dropdown's own event handler).
		if (m_slotsDirty) {
			m_slotsDirty = false;
			rebuildSlotContainer();
		}

		// Left pane: batch list.
		Element leftPane =
			window(text("Batches") | bold | color(t->title),
				   m_batchNames.empty()
					   ? (text("  (none) — click New") | color(t->toggleOff))
					   : batchMenu->Render()) |
			size(WIDTH, GREATER_THAN, 26);

		// Right pane: editor.
		Elements rightRows;
		rightRows.push_back(
			ui_utils::settingRowComponent("Name", nameInput->Render()));
		rightRows.push_back(separatorEmpty());
		rightRows.push_back(ui_utils::numberRow("Count",
												countMinus->Render(),
												countInput->Render(),
												countPlus->Render(),
												m_editCountStr));
		rightRows.push_back(separatorEmpty());
		rightRows.push_back(text("Presets") | color(t->label));
		if (m_allPresetNames.empty())
			rightRows.push_back(text("  (no presets defined)") |
								color(t->toggleOff));
		else
			rightRows.push_back(m_slotContainer->Render());
		rightRows.push_back(separatorEmpty());
		rightRows.push_back(separatorLight());
		rightRows.push_back(separatorEmpty());
		rightRows.push_back(hbox({
			filler(),
			newBtn->Render(),
			separatorEmpty(),
			saveBtn->Render(),
			separatorEmpty(),
			deleteBtn->Render(),
			filler(),
		}));

		// Push the bottom row to the very bottom of the pane.
		rightRows.push_back(filler());
		rightRows.push_back(separatorLight());

		// Bottom row inside the box: status text bottom-left, LOAD/UNLOAD
		// button bottom-right.
		Element statusEl =
			m_status.empty() ? text("") : text(m_status) | color(t->label);
		rightRows.push_back(hbox({
			statusEl | vcenter,
			filler(),
			loadBtn->Render() | size(WIDTH, GREATER_THAN, 14),
		}));

		Element rightPane = window(text("Edit Batch") | bold | color(t->title),
								   vbox(std::move(rightRows))) |
							flex;

		return hbox({ leftPane, separator(), rightPane }) | flex;
	});

	return m_component;
}
