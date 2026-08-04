#pragma once

#include "IBatchStore.h"
#include "ILlamaServerProcess.h"
#include "IModelStateTracker.h"
#include "IModelsIni.h"
#include "appDependencies.h"

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#include <memory>
#include <string>
#include <vector>

/**
 * @file batchesPanel.h
 * @brief Panel for creating, deleting, and loading/unloading named preset
 *        batches (issue #111).
 *
 * A *batch* is a named, ordered collection of preset (models.ini section)
 * names. This panel lists saved batches on the left and, for the highlighted
 * batch, shows its presets plus a build-a-batch editor on the right: a themed
 * dropdown per preset slot followed by a trailing "add" dropdown that appends a
 * new slot when a preset is chosen. The same preset may be added more than once.
 *
 * Loading a batch loads each of its presets individually via the shared
 * load primitives (tracker intent + llama-server load), mirroring the control
 * API's no-poll load path. The user is responsible for ensuring the batch fits
 * their hardware.
 *
 * Dependencies are injected via constructor (no singleton calls).
 */
class BatchesPanel
{
  public:
	/**
	 * @brief Constructs the BatchesPanel with injected dependencies.
	 * @param deps Dependencies (batch store, models.ini, server, tracker).
	 */
	explicit BatchesPanel(AppDependencies &deps);

	BatchesPanel(const BatchesPanel &) = delete;
	BatchesPanel &operator=(const BatchesPanel &) = delete;
	BatchesPanel(BatchesPanel &&) = delete;
	BatchesPanel &operator=(BatchesPanel &&) = delete;

	/**
	 * @brief Returns the FTXUI component for this panel (cached after first
	 *        creation).
	 */
	ftxui::Component component();

	/**
	 * @brief Called when the user navigates TO this panel's tab.
	 *
	 * Refreshes the preset list from models.ini (it may have been mutated by the
	 * Model Settings panel while this panel was hidden) and rebuilds the slot
	 * dropdowns if the set of presets changed.
	 */
	void onShown();

  private:
	// Grants the unit test access to private action methods so behavior can be
	// driven directly without simulating UI focus navigation.
	friend class BatchesPanelTest;

	// Injected dependencies
	IBatchStore &m_batches;
	IModelsIni &m_modelsIni;
	ILlamaServerProcess &m_server;
	IModelStateTracker &m_tracker;
	IBatchStateTracker &m_batchTracker;

	// =========================================================================
	// State
	// =========================================================================
	ftxui::Component m_component;

	/** Saved batch names (left-hand list). */
	std::vector<std::string> m_batchNames;
	/** Highlighted batch index, or -1 if none. */
	int m_selectedBatchIndex = -1;
	/** Last index the render loop loaded into the editor (selection-change
	 *  detection; MenuOption::on_change is unreliable for the single-batch case
	 *  where the index never changes). */
	int m_lastBatchIndex = -1;

	/** All selectable preset (section) names, for the slot dropdowns. */
	std::vector<std::string> m_allPresetNames;

	/** Name of the batch being edited (bound to the name Input). */
	std::string m_editName;
	/** Ordered preset section names in the batch being edited; size == count. */
	std::vector<std::string> m_editPresets;

	/** Number of preset rows in the editor (1–10). */
	int m_editCount = 1;
	std::string m_editCountStr = "1";

	/** Status/feedback message shown under the editor ("" = none). */
	std::string m_status;

	/** Bottom LOAD/UNLOAD button label (LOAD/UNLOAD/LOADING). */
	std::string m_loadLabel = "LOAD";

	static constexpr int MIN_COUNT = 1;
	static constexpr int MAX_COUNT = 10;

	// Per-slot dropdown backing storage. Heap-allocated so addresses stay stable
	// across rebuilds (FTXUI dropdowns hold raw int*/vector* into these). Each
	// slot has its OWN options vector = all presets minus those picked in the
	// other rows (no preset can appear in a batch twice).
	std::vector<std::unique_ptr<int>> m_slotIdx;
	std::vector<std::unique_ptr<std::vector<std::string>>> m_slotOptions;
	ftxui::Component m_slotContainer; ///< holds the per-slot dropdowns
	/** Set when a slot pick changed; the renderer rebuilds (re-prunes) next
	 *  frame rather than mid-event (which would free the live dropdown). */
	bool m_slotsDirty = false;

	/** @return first preset not used by any row except @p exceptRow, or "". */
	[[nodiscard]] std::string firstUnusedPreset(int exceptRow) const;

	/** @return effective max preset count: min(10, number of presets). */
	[[nodiscard]] int maxCount() const;

	// =========================================================================
	// Actions
	// =========================================================================

	/** Reload m_batchNames from the store; clamp selection. */
	void refreshBatchNames();

	/** Reload m_allPresetNames from models.ini. @return true if the set changed.
	 */
	bool refreshPresetNames();

	/**
	 * @brief Populate the editor on construction.
	 *
	 * If a batch is selected, loads it into the editor; otherwise starts a fresh
	 * one-row batch. Without this the editor renders empty until the Menu's
	 * on_change fires, which never happens for the initial/single-batch case.
	 */
	void initEditor();

	/** Clear the editor to start a new batch (1 default row). */
	void startNewBatch();

	/** Load the highlighted batch into the editor for viewing/editing. */
	void loadSelectedIntoEditor();

	/**
	 * @brief Resize the editor to @p count preset rows (clamped 1–10).
	 *
	 * Growing appends rows defaulting to the first preset; shrinking removes
	 * rows from the BOTTOM, leaving earlier picks untouched. Rebuilds the slot
	 * container.
	 */
	void setPresetCount(int count);

	/** Rebuild the slot dropdown container from m_editPresets. */
	void rebuildSlotContainer();

	/** Persist the editor's batch (name + presets) to the store. */
	void saveEditedBatch();

	/** Delete the highlighted batch from the store. */
	void deleteSelectedBatch();

	/** Load every preset in the highlighted batch (in order); mark loaded. */
	void loadSelectedBatch();

	/** Unload every preset in the highlighted batch; mark unloaded. */
	void unloadSelectedBatch();

	/** Bottom-button click: LOAD or UNLOAD per current label. */
	void onLoadUnloadClicked();

	/** Recompute m_loadLabel from the selected batch's live status. */
	void updateLoadLabel();

	/** @return section name of the highlighted batch, or "" if none. */
	[[nodiscard]] std::string selectedBatchName() const;
};
