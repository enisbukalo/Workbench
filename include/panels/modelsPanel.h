#pragma once

#include "IConfigManager.h"
#include "IGpuMonitor.h"
#include "ILlamaServerProcess.h"
#include "IModelInfoMonitor.h"
#include "IModelStateTracker.h"
#include "IModelsIni.h"
#include "appDependencies.h"

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

/**
 * @file modelsPanel.h
 * @brief Panel containing Load and Inference configuration settings.
 *
 * This panel provides interactive controls for configuring model loading
 * parameters and inference behavior. It is a stateful component that
 * reads from and writes to the ConfigManager on change.
 *
 * Dependencies are injected via constructor (no singleton calls).
 *
 * Sections:
 * - Load Settings: Model path, GPU layers, context size, batch size,
 *   flash attention, memory mapping options
 * - Inference Settings: Temperature, top-P, top-K, min-P, penalties,
 *   and max tokens prediction
 */
class ModelsPanel
{
  public:
	/**
	 * @brief Constructs the ModelsPanel with injected dependencies.
	 *
	 * @param deps Dependencies required by this panel (config, server,
	 *             model info monitor, models INI).
	 * Initializes all member variables from config values.
	 */
	explicit ModelsPanel(AppDependencies &deps);

	/**
	 * @brief Destructor; joins any in-flight async state poll.
	 *
	 * The async worker checks m_asyncStop every poll tick, so destruction
	 * blocks at most one tick (~500 ms) instead of leaving a detached thread
	 * that would touch a destroyed panel (use-after-free on app exit).
	 */
	~ModelsPanel();

	ModelsPanel(const ModelsPanel &) = delete;
	ModelsPanel &operator=(const ModelsPanel &) = delete;
	ModelsPanel(ModelsPanel &&) = delete;
	ModelsPanel &operator=(ModelsPanel &&) = delete;

	/**
	 * @brief Returns the FTXUI component for this panel.
	 *
	 * The component is cached after first creation and reused on
	 * subsequent calls.
	 *
	 * @return An @c ftxui::Component containing the Load and Inference
	 *         settings controls.
	 */
	ftxui::Component component();

  private:
	// Grants the unit test access to private action methods (e.g.
	// saveCurrentToPreset) so behavior can be driven directly without
	// simulating fragile UI focus navigation.
	friend class ModelsPanelTest;

	// Injected dependencies
	IConfigManager &m_config;
	ILlamaServerProcess &m_server;
	IModelInfoMonitor &m_modelInfo;
	IModelStateTracker &m_tracker;
	IModelsIni &m_modelsIni;
	IGpuMonitor &m_gpu;
	/**
	 * @brief Loads current configuration values into member variables.
	 *
	 * Called from constructor to initialize state. Reads Load and
	 * Inference sections from ConfigManager.
	 */
	void loadFromConfig();

	/**
	 * @brief Parse comma-separated tensor-split string into per-GPU fields.
	 *
	 * Sizes m_tensorSplitFields to gpuCount exactly once (constructor path).
	 * On subsequent calls (preset apply) mutates values in-place without resize
	 * — FTXUI Input components hold raw pointers into the vector.
	 *
	 * @param raw   Comma-separated ratio string from config, e.g. "50,20".
	 * @param gpuCount Number of GPU fields to populate (fixed at construction).
	 * @pre gpuCount >= 1
	 */
	void initTensorSplitFromConfig(const std::string &raw, int gpuCount);

	/**
	 * @brief Serialize per-GPU fields back to comma-separated string.
	 *
	 * Applies roundToHundredth to each field. Does NOT mutate
	 * m_tensorSplitFields.
	 *
	 * @return Comma-separated string, e.g. "1.50,60.30".
	 */
	[[nodiscard]] std::string tensorSplitToString() const;

	/**
	 * @brief Parse @p s as a non-negative decimal and format to two decimal
	 * places.
	 *
	 * Uses std::locale::classic() to avoid locale-dependent decimal separators.
	 * Returns "1.00" if @p s is empty, non-numeric, or negative.
	 *
	 * @param s Input string, e.g. "1.5", "60", "abc".
	 * @return Rounded string, e.g. "1.50", "60.00", "1.00".
	 */
	[[nodiscard]] static std::string roundToHundredth(const std::string &s);

	/**
	 * @brief onChange hook fired when a load/inference form field changes.
	 *
	 * Load/inference settings are persisted to a models.ini preset only on an
	 * explicit save (saveCurrentToPreset()), not on every edit, so this hook
	 * does not write anything. Retained for future per-field side effects.
	 */
	void onFormChanged();

	// =========================================================================
	// Member component
	// =========================================================================
	ftxui::Component m_component;

	// =========================================================================
	// Load Settings State
	// =========================================================================
	std::string m_modelPath;
	std::string m_ngpuLayers;
	std::string m_ctxSize;
	int m_batchSize = 2048;
	std::string m_batchSizeStr = "2048";
	int m_ubatchSize = 512;
	std::string m_ubatchSizeStr = "512";
	int m_parallel = 4; // -1=auto, or number of slots
	std::string m_parallelStr = "4";
	int m_flashAttnIdx = 0; // 0=auto, 1=on, 2=off
	bool m_kvOffload = true;
	bool m_kvUnified = true;
	bool m_mmap = false;
	bool m_mlock = false;
	bool m_fit = true;
	std::string m_devicePriority = ""; // ""=auto, "0"=GPU0 first, "1"=GPU1 first
	std::vector<std::string>
		m_tensorSplitFields; // one per GPU, e.g. ["1.50","60.30"]
	std::vector<ftxui::Component> m_tensorSplitInputs; // FTXUI Input per GPU
	std::string m_cacheTypeK = "f16";
	std::string m_cacheTypeV = "f16";
	std::string m_lora;
	std::string m_mmproj;
	std::string m_modelDraft;
	std::string m_draftMax = "-1";
	std::string m_specType;
	std::string m_cacheTypeKDraft = "f16";
	std::string m_cacheTypeVDraft = "f16";
	std::string m_deviceDraft; // ""=auto, e.g. "CUDA0,CUDA1"
	bool m_preserveThinking = false;
	std::string m_chatTemplate;
	std::string m_reasoningFormat;

	// Issue #103 section C — additional load params
	int m_nCpuMoe = -1;
	std::string m_nCpuMoeStr = "-1";
	bool m_cpuMoe = false;
	std::string m_overrideTensor;
	int m_ropeScalingIdx = 0; // index into m_ropeScalingOptions ("(default)")
	double m_ropeScale = 0.0;
	std::string m_ropeScaleStr = "0";
	double m_ropeFreqBase = 0.0;
	std::string m_ropeFreqBaseStr = "0";
	double m_ropeFreqScale = 0.0;
	std::string m_ropeFreqScaleStr = "0";
	int m_yarnOrigCtx = 0;
	std::string m_yarnOrigCtxStr = "0";
	double m_yarnExtFactor = -1.0;
	std::string m_yarnExtFactorStr = "-1";
	double m_yarnAttnFactor = 1.0;
	std::string m_yarnAttnFactorStr = "1";
	double m_yarnBetaSlow = 1.0;
	std::string m_yarnBetaSlowStr = "1";
	double m_yarnBetaFast = 32.0;
	std::string m_yarnBetaFastStr = "32";
	bool m_swaFull = false;
	int m_keep = -1;
	std::string m_keepStr = "-1";
	int m_numaIdx = 0;		// index into m_numaOptions ("(default)")
	int m_fitTargetIdx = 0; // index into m_fitTargetOptions ("(default)")
	int m_fitCtx = 0;
	std::string m_fitCtxStr = "0";
	bool m_checkTensors = false;
	std::string m_overrideKv;
	std::string m_loraScaled;
	std::string m_controlVector;
	std::string m_controlVectorScaled;
	int m_specDraftNMin = -1;
	std::string m_specDraftNMinStr = "-1";
	double m_specDraftPMin = -1.0;
	std::string m_specDraftPMinStr = "-1";
	double m_specDraftPSplit = -1.0;
	std::string m_specDraftPSplitStr = "-1";
	bool m_cpuMoeDraft = false;

	// =========================================================================
	// Inference Settings State
	// =========================================================================
	float m_temperature = 0.8f;
	std::string m_temperatureStr;
	float m_topP = 0.95f;
	std::string m_topPStr;
	int m_topK = 40;
	std::string m_topKStr = "40";
	float m_minP = 0.05f;
	std::string m_minPStr;
	float m_repeatPenalty = 1.0f;
	std::string m_repeatPenaltyStr;
	float m_presencePenalty = 0.0f;
	std::string m_presencePenaltyStr;
	float m_frequencyPenalty = 0.0f;
	std::string m_frequencyPenaltyStr;
	std::string m_nPredict;
	std::string m_seed = "-1"; // -1 = random

	// Phase 4 inference params (section D)
	bool m_ignoreEos = false;
	std::string m_logitBias;
	double m_adaptiveTarget = 0.0;
	std::string m_adaptiveTargetStr;
	double m_adaptiveDecay = 0.0;
	std::string m_adaptiveDecayStr;
	std::string m_grammarFile;
	std::string m_jsonSchemaFile;
	std::string m_samplerSeq;
	std::string m_drySequenceBreaker;
	bool m_backendSampling = false;

	// =========================================================================
	// Dropdown Options
	// =========================================================================
	std::vector<std::string> m_flashAttnOptions = { "auto", "on", "off" };
	std::vector<std::string> m_splitModeOptions = { "none", "layer", "row" };
	std::vector<std::string> m_cacheTypeOptions = { "f16",	  "f32",  "bf16",
													"q8_0",	  "q4_0", "q4_1",
													"iq4_nl", "q5_0", "q5_1" };
	std::vector<std::string> m_reasoningFormatOptions = {
		"auto", "default", "none", "hidden", "deepseek", "deepseek-legacy"
	};
	const std::vector<std::string> m_specTypeOptions = {
		"none",			"draft-simple", "draft-eagle3", "draft-mtp",
		"draft-dflash", "ngram-simple", "ngram-map-k",	"ngram-map-k4v",
		"ngram-mod",	"ngram-cache"
	};
	// Section-C enums. Index 0 of each maps back to "" (omitted).
	std::vector<std::string> m_ropeScalingOptions = { "(default)",
													  "none",
													  "linear",
													  "yarn" };
	std::vector<std::string> m_numaOptions = { "(default)",
											   "distribute",
											   "isolate",
											   "numactl" };
	std::vector<std::string> m_fitTargetOptions = { "(default)",
													"auto",
													"vram",
													"ram" };
	int m_splitModeIdx = 1;		  // 1 = "layer" (default)
	int m_cacheTypeKIdx = 0;	  // 0 = "f16" (default)
	int m_cacheTypeVIdx = 0;	  // 0 = "f16" (default)
	int m_reasoningFormatIdx = 0; // 0 = "auto" (default)
	int m_specTypeIdx = 0;		  // 0 = "none" (default)
	int m_cacheTypeKDraftIdx = 0; // index of "f16" in m_cacheTypeOptions
	int m_cacheTypeVDraftIdx = 0; // index of "f16" in m_cacheTypeOptions

	// =========================================================================
	// Model Selection (from models.ini)
	// =========================================================================
	std::vector<std::string> m_modelNames; // Section names from models.ini (used
										   // for /models/load API)
	std::vector<std::string> m_modelDisplayNames; // Display names for dropdown
												  // (same as names for now)
	std::vector<std::string>
		m_modelPaths;				 // GGUF file paths parallel to m_modelNames
	int m_modelDropdownIndex = 0;	 // Selected index in dropdown
	std::string m_selectedModelName; // Section name of selected model

	/** Refresh model list from ModelsIni singleton. */
	void refreshModelList();

	// =========================================================================
	// Preset State
	// =========================================================================
	std::vector<Config::ModelPreset>
		m_presetsForModel;						   // filtered to current model
	std::vector<std::string> m_presetDisplayNames; // names for Menu component
	int m_selectedPresetIndex = -1;				   // -1 = none selected
	std::string m_editingPresetName;			   // bound to editable input
	std::string m_presetStatus; // status message (e.g. "Saved", "Name in use")

	/** Reload presets from ModelsIni for the currently selected model. */
	void refreshPresetsForModel();

	/**
	 * Auto-select and apply the first preset for the current model (or clear
	 * selection if none exist). Used on first load and on model change.
	 */
	void autoSelectFirstPreset();

	/** Apply a preset's load + inference values into all member state. */
	void applyPreset(const Config::ModelPreset &preset);

	/** Write current member state back to the selected preset in models.ini. */
	void saveCurrentToPreset();

	/** Rename the selected preset. */
	void renameSelectedPreset(const std::string &newName);

	/**
	 * @brief Delete the selected preset from models.ini.
	 *
	 * After deletion, refreshes the preset list. If that was the model's last
	 * preset, the model no longer has any models.ini entry and cannot be
	 * loaded, so the model list/dropdown is refreshed too and selection moves
	 * to a still-present model (issue #78 follow-up).
	 */
	void deleteSelectedPreset();

	// =========================================================================
	// Add-New-Model Popup State
	// =========================================================================
	/** True while the "Add New Model" modal is shown. Drives ftxui::Modal. */
	bool m_showAddModelPopup = false;
	/** Bound to the popup's path Input. */
	std::string m_addModelPath;
	/** Validation/save feedback shown inside the popup ("" = none). */
	std::string m_addModelError;

	/** Open the popup: clear path/error and show the modal. */
	void openAddModelPopup();

	/**
	 * @brief Validate m_addModelPath, write a default preset to models.ini,
	 *        refresh the model list, and select the new entry.
	 *
	 * On validation or save failure, sets m_addModelError and leaves the popup
	 * open. On success, closes the popup. The new models.ini section is named
	 * "<derived-name>_DEFAULT" and carries struct-default load/inference values.
	 */
	void confirmAddModel();

	/** Close the popup without writing anything. */
	void cancelAddModel();

	/**
	 * @brief Check if a model path should be filtered out based on fileFilter
	 * patterns.
	 *
	 * Implements glob-style wildcard matching (case-insensitive).
	 *
	 * @param path Full or partial path to the model file
	 * @return true if the model should be filtered out (excluded), false
	 * otherwise
	 */
	bool shouldFilterModel(const std::string &path) const;

	// =========================================================================
	// Server/Model State Tracking (for single server button)
	// =========================================================================
	// Thread model: the async worker thread writes ONLY the two atomics
	// below; everything else in this section is UI-thread-only. The render
	// loop watches the atomics and refreshes the plain state on the edge
	// where a worker finishes, so no std::string is ever written off-thread.

	/** True while waiting for server to become healthy after launch.
	 *  Written by the async worker; read by the UI thread. */
	std::atomic<bool> m_serverStarting{ false };

	/** True while a model-load API call + confirmation poll is in flight.
	 *  Written by the async worker; read by the UI thread. */
	std::atomic<bool> m_modelLoading{ false };

	/** Async state-poll worker (server-start health poll or model-load
	 *  confirmation poll; the two are mutually exclusive). Joined before
	 *  reuse and in the destructor — never detached. */
	std::thread m_asyncPoll;

	/** Cooperative stop for m_asyncPoll, checked every poll tick. */
	std::atomic<bool> m_asyncStop{ false };

	/** Join m_asyncPoll if joinable (signals m_asyncStop first). */
	void joinAsyncPoll();

	/** Current server running state. @note UI thread only. */
	bool m_serverRunning = false;

	/**
	 * @brief Section name of the currently selected preset, or "" if none.
	 *
	 * The model dropdown selects a GGUF; the preset Menu selects the section
	 * within it. Load/unload act on this section (#110). Falls back to the model
	 * name when no preset is highlighted.
	 *
	 * @return Selected preset section name, or "" when nothing is selectable.
	 */
	[[nodiscard]] std::string selectedPresetSection() const;

	/** @brief True when @ref selectedPresetSection is reported LOADED by the
	 *  tracker (read live via the monitor; no cached panel state). */
	[[nodiscard]] bool isSelectedPresetLoaded() const;

	/** @brief Count of presets the tracker currently reports LOADED. Live read;
	 *  used to detect "unloading the last model" (#71). */
	[[nodiscard]] std::size_t loadedPresetCount() const;

	/** Dynamic label for server button: LOAD -> STOP. @note UI thread only. */
	std::string m_startStopLabel = "LOAD";

	/** Handle server button click */
	void onStartStopClicked();

	/** Handle LOAD/UNLOAD (when server running) */
	void onLoadUnloadClicked();

	/** Refresh server and model state from API */
	void refreshServerState();

	/**
	 * @brief Restart the running server with no model loaded.
	 *
	 * Mirrors the preset-save restart: silence the monitor (#71), terminate,
	 * relaunch in router mode with an empty model, and reset UI state to LOAD.
	 * Used both after a preset save and after an auto-detected model crash.
	 */
	void restartServer();

	/** Update button label based on server state and selected model */
	void updateStartStopLabel();
};
