#include "modelsPanel.h"

#include "ThemeManager.h"
#include "modelDiscovery.h"
#include "modelPathValidation.h"
#include "ui_utils.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <locale>
#include <sstream>
#include <thread>

using namespace ftxui;

namespace {
// Per-section scroll caps: max terminal lines a section's scrollable row
// viewport occupies before a vertical scrollbar appears. LESS_THAN is
// exclusive; the window border/indent are outside the scrolled vbox. The Load
// section has the most rows, so its cap is higher; on short terminals each
// section scrolls internally so every control stays reachable.
constexpr int LOAD_SETTINGS_MAX_LINES = 22;
constexpr int INFERENCE_SETTINGS_MAX_LINES = 16;
} // namespace

/**
 * @brief Find the index of a value in an options vector, or 0 if not found.
 */
static int findOptionIndex(const std::vector<std::string> &options,
						   const std::string &value)
{
	for (size_t i = 0; i < options.size(); ++i)
		if (options[i] == value)
			return static_cast<int>(i);
	return 0;
}

// =========================================================================
// Constructor and Config Methods
// =========================================================================

ModelsPanel::ModelsPanel(AppDependencies &deps)
	: m_config(deps.config), m_server(deps.server), m_modelInfo(deps.modelInfo),
	  m_tracker(deps.tracker), m_modelsIni(deps.modelsIni), m_gpu(deps.gpu)
{
	loadFromConfig();
}

ModelsPanel::~ModelsPanel()
{
	joinAsyncPoll();
}

void ModelsPanel::joinAsyncPoll()
{
	if (m_asyncPoll.joinable()) {
		m_asyncStop.store(true, std::memory_order_release);
		m_asyncPoll.join();
		m_asyncStop.store(false, std::memory_order_release);
	}
}

void ModelsPanel::loadFromConfig()
{
	spdlog::debug("Initializing models panel from models.ini");

	// Probe GPU count once at construction time, then size the tensor-split
	// fields. This MUST happen before any applyPreset() call: applyPreset ->
	// initTensorSplitFromConfig mutates m_tensorSplitFields in-place and never
	// resizes (FTXUI Input components hold raw pointers into the vector).
	//
	// SystemMonitorRunner::start() is called before ModelsPanel construction in
	// app.cpp, but the first background poll (250 ms) may not have fired yet —
	// so the explicit update() here is required, not redundant.
	// update() is not noexcept (may call NVML); wrap to prevent constructor
	// throw.
	try {
		m_gpu.update();
	} catch (...) {
		spdlog::warn(
			"GPU probe failed in ModelsPanel — defaulting to 1 GPU field");
	}
	int gpuCount = std::max(1, static_cast<int>(m_gpu.getStats().size()));
	initTensorSplitFromConfig("", gpuCount);

	// Seed the form with defaults so every field (including the string mirrors
	// the FTXUI inputs render) is populated even when the selected model has no
	// presets yet. A real preset, if any, overrides this below.
	applyPreset(Config::ModelPreset{});

	// Refresh model list from models.ini and select the first model.
	refreshModelList();
	if (!m_modelNames.empty()) {
		if (m_modelDropdownIndex < 0 ||
			m_modelDropdownIndex >= static_cast<int>(m_modelNames.size()))
			m_modelDropdownIndex = 0;
		m_selectedModelName = m_modelNames[m_modelDropdownIndex];
		m_modelPath = m_modelPaths[m_modelDropdownIndex];
	}

	// Load presets for the selected model and apply the first one (if any) so
	// the form reflects the model's saved settings from models.ini.
	refreshPresetsForModel();
	autoSelectFirstPreset();
}

void ModelsPanel::onFormChanged()
{
	// Load/inference edits live only in the form's member state until the user
	// explicitly saves them to a models.ini preset via saveCurrentToPreset().
	// config.json no longer stores load/inference, so there is nothing to
	// persist on every keystroke — the input bindings have already updated the
	// m_* members. This hook is retained for future per-field side effects.
}

// =========================================================================
// Model Discovery Integration
// =========================================================================

/**
 * @brief Check if a model path should be filtered out based on fileFilter
 * patterns.
 *
 * Implements glob-style wildcard matching (case-insensitive):
 * - `mmproj*` → matches filenames starting with "mmproj"
 * - `*mmproj` → matches filenames ending with "mmproj"
 * - `*mmproj*` → matches filenames containing "mmproj"
 * - No `*` → substring match anywhere in filename
 *
 * @param path Full or partial path to the model file
 * @return true if the model should be filtered out (excluded), false otherwise
 */
bool ModelsPanel::shouldFilterModel(const std::string &path) const
{
	// Extract just the filename from the path
	std::string filename = path;
	size_t lastSlash = path.find_last_of("/\\");
	if (lastSlash != std::string::npos) {
		filename = path.substr(lastSlash + 1);
	}

	// Convert filename to lowercase for case-insensitive matching
	std::string lowerFilename;
	lowerFilename.reserve(filename.size());
	for (char c : filename) {
		lowerFilename +=
			static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}

	// Get filter patterns from config
	const auto cfg = m_config.getConfigSnapshot();
	const auto &filters = cfg.discovery.fileFilter;

	// Check each filter pattern - if ANY matches, filter out the model
	for (const auto &pattern : filters) {
		if (pattern.empty()) {
			continue;
		}

		// Convert pattern to lowercase
		std::string lowerPattern;
		lowerPattern.reserve(pattern.size());
		for (char c : pattern) {
			lowerPattern +=
				static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}

		// Find position of '*'
		size_t starPos = lowerPattern.find('*');

		if (starPos == std::string::npos) {
			// No wildcard: substring match
			if (lowerFilename.find(lowerPattern) != std::string::npos) {
				return true;
			}
		} else if (starPos == 0 && lowerPattern.back() == '*') {
			// *pattern*: contains match
			std::string searchStr =
				lowerPattern.substr(1, lowerPattern.size() - 2);
			if (lowerFilename.find(searchStr) != std::string::npos) {
				return true;
			}
		} else if (starPos == 0) {
			// *pattern: ends with
			std::string suffix = lowerPattern.substr(1);
			if (lowerFilename.size() >= suffix.size() &&
				lowerFilename.compare(lowerFilename.size() - suffix.size(),
									  suffix.size(),
									  suffix) == 0) {
				return true;
			}
		} else {
			// pattern*: starts with
			std::string prefix = lowerPattern.substr(0, starPos);
			if (lowerFilename.rfind(prefix, 0) == 0) {
				return true;
			}
		}
	}

	return false;
}

void ModelsPanel::refreshModelList()
{
	auto entries = m_modelsIni.getUniqueModelEntries();

	// Sort by display name
	std::sort(entries.begin(),
			  entries.end(),
			  [](const ModelsIniEntry &a, const ModelsIniEntry &b) {
				  return a.displayName < b.displayName;
			  });

	m_modelNames.clear();
	m_modelDisplayNames.clear();
	m_modelPaths.clear();
	for (const auto &entry : entries) {
		m_modelNames.push_back(entry.displayName);
		m_modelDisplayNames.push_back(entry.displayName);
		m_modelPaths.push_back(entry.modelPath);
	}

	// Reset dropdown index if it's now out of bounds
	if (m_modelDropdownIndex < 0 ||
		m_modelDropdownIndex >= static_cast<int>(m_modelNames.size())) {
		m_modelDropdownIndex = 0;
		if (!m_modelNames.empty()) {
			m_selectedModelName = m_modelNames[0];
		}
	}
}

// =========================================================================
// Tensor Split Helpers
// =========================================================================

void ModelsPanel::initTensorSplitFromConfig(const std::string &raw, int gpuCount)
{
	// Parse tokens
	std::vector<std::string> tokens;
	std::istringstream ss(raw);
	std::string tok;
	while (std::getline(ss, tok, ','))
		tokens.push_back(tok);

	// Size the vector exactly once (constructor path, before component() built).
	// On preset-apply path, m_tensorSplitFields is already sized — only mutate.
	// Input components hold raw pointers into this vector — never resize after
	// build.
	if (static_cast<int>(m_tensorSplitFields.size()) != gpuCount)
		m_tensorSplitFields.resize(gpuCount);

	for (int i = 0; i < gpuCount; ++i) {
		if (i < static_cast<int>(tokens.size()) && !tokens[i].empty())
			m_tensorSplitFields[i] = roundToHundredth(tokens[i]);
		else
			m_tensorSplitFields[i] = "1.00";
	}
}

[[nodiscard]] std::string ModelsPanel::tensorSplitToString() const
{
	std::string result;
	for (size_t i = 0; i < m_tensorSplitFields.size(); ++i) {
		if (i > 0)
			result += ',';
		result += roundToHundredth(m_tensorSplitFields[i]);
	}
	return result;
}

[[nodiscard]] std::string ModelsPanel::roundToHundredth(const std::string &s)
{
	std::istringstream iss(s);
	iss.imbue(std::locale::classic());
	double v = 0.0;
	if (!(iss >> v) || v < 0.0)
		return "1.00";
	std::ostringstream oss;
	oss.imbue(std::locale::classic());
	oss << std::fixed << std::setprecision(2) << v;
	return oss.str();
}

// =========================================================================
// Component Method
// =========================================================================

Component ModelsPanel::component()
{
	if (m_component)
		return m_component;

	// Refresh server state on first creation (server may already be running from
	// app startup)
	refreshServerState();
	updateStartStopLabel();

	auto onChange = [this] { onFormChanged(); };

	// -----------------------------------------------------------------------
	// Shared options — colors resolved from the active theme at render time.
	// Each transform re-reads getActive() so a live theme switch is picked up
	// on the next frame instead of being frozen at component-build time.
	// -----------------------------------------------------------------------
	InputOption inputOpt;
	inputOpt.on_change = onChange;
	inputOpt.multiline = false;
	inputOpt.transform = [](InputState state) {
		auto theme = ThemeManager::instance().getActive();
		auto e = state.element | align_right;
		if (state.is_placeholder)
			return e | color(theme->toggleOff);
		return e | color(theme->toggleOn);
	};

	auto btnStyle = ButtonOption::Animated();
	btnStyle.transform = [](const EntryState &s) {
		auto theme = ThemeManager::instance().getActive();
		auto e = text(s.label) | color(theme->toggleOn);
		if (s.focused)
			e |= bold;
		return e | center;
	};

	auto loadBtnStyle = ButtonOption::Animated();
	loadBtnStyle.transform = [](const EntryState &s) {
		auto theme = ThemeManager::instance().getActive();
		auto e = text(s.label) | color(theme->toggleOn);
		if (s.focused)
			e |= bold;
		e |= bgcolor(theme->activeTabBg);
		return e | center;
	};

	// Start/Stop button style - Green text when stopped (LOAD), Red when running
	// (UNLOAD/STOP)
	auto startStopBtnStyle = ButtonOption::Animated();
	startStopBtnStyle.transform = [](const EntryState &s) {
		auto theme = ThemeManager::instance().getActive();
		Color textColor;
		if (s.label == "STARTING...")
			textColor = theme->warning;
		else if (s.label == "LOADING")
			textColor = theme->warning;
		else if (s.label == "LOAD")
			textColor = theme->success;
		else
			textColor = theme->error;

		auto e = text(s.label) | color(textColor);
		if (s.label == "LOADING")
			e |= dim; // FTXUI dim modifier reduces visual weight
		if (s.focused)
			e |= bold;
		return e | center;
	};

	// Load/Unload button style (activeTabBg for LOAD, selectionBg for UNLOAD)
	auto loadUnloadBtnStyle = ButtonOption::Animated();
	loadUnloadBtnStyle.transform = [](const EntryState &s) {
		auto theme = ThemeManager::instance().getActive();
		auto e = text(s.label) | color(theme->toggleOn);
		if (s.focused)
			e |= bold;
		// activeTabBg for LOAD, selectionBg for UNLOAD
		auto label = s.label;
		if (label == "UNLOAD") {
			e |= bgcolor(theme->selectionBg);
		} else {
			e |= bgcolor(theme->activeTabBg);
		}
		return e | center;
	};

	CheckboxOption cbOpt;
	cbOpt.on_change = onChange;
	cbOpt.transform = [](const EntryState &s) {
		auto theme = ThemeManager::instance().getActive();
		auto label = s.state ? text("[X]") : text("[ ]");
		if (s.state)
			label |= color(theme->toggleOn);
		else
			label |= color(theme->toggleOff);
		if (s.focused)
			label |= bold;
		return label;
	};

	// Helper: create [-] [input] [+] for an int field
	// Creates a triplet of components: decrement button, text input, increment
	// button Value is clamped to [minVal, maxVal] range on every change
	// Immediately persists to config via onChange callback
	auto makeIntControls =
		[&](int &value, std::string &str, int minVal, int maxVal, int step) {
			struct Controls
			{
				Component minus, input, plus;
			};
			auto minus = Button(
				"-",
				[&value, &str, minVal, step, onChange] {
					value = std::max(minVal, value - step);
					str = std::to_string(value);
					onChange();
				},
				btnStyle);
			auto plus = Button(
				"+",
				[&value, &str, maxVal, step, onChange] {
					value = std::min(maxVal, value + step);
					str = std::to_string(value);
					onChange();
				},
				btnStyle);
			InputOption numInputOpt = inputOpt;
			numInputOpt.transform = [](InputState state) {
				auto theme = ThemeManager::instance().getActive();
				auto e = state.element | center;
				if (state.is_placeholder)
					return e | color(theme->toggleOff);
				return e | color(theme->toggleOn);
			};
			numInputOpt.on_change = [&value, &str, minVal, maxVal, onChange] {
				try {
					int v = std::stoi(str);
					value = std::clamp(v, minVal, maxVal);
				} catch (...) {
				}
				onChange();
			};
			auto inp = Input(&str, "", numInputOpt);
			return Controls{ minus, inp, plus };
		};

	// Helper: create [-] [input] [+] for a float field
	// Same pattern as makeIntControls but for floating-point values
	// Uses ui_utils::formatFloat for consistent decimal representation
	// Useful for probability/temperature parameters (e.g., temperature, top_p)
	auto makeFloatControls = [&](float &value,
								 std::string &str,
								 float minVal,
								 float maxVal,
								 float step) {
		struct Controls
		{
			Component minus, input, plus;
		};
		auto minus = Button(
			"-",
			[&value, &str, minVal, step, onChange] {
				value = std::max(minVal, value - step);
				str = ui_utils::formatFloat(value);
				onChange();
			},
			btnStyle);
		auto plus = Button(
			"+",
			[&value, &str, maxVal, step, onChange] {
				value = std::min(maxVal, value + step);
				str = ui_utils::formatFloat(value);
				onChange();
			},
			btnStyle);
		InputOption numInputOpt = inputOpt;
		numInputOpt.transform = [](InputState state) {
			auto theme = ThemeManager::instance().getActive();
			auto e = state.element | center;
			if (state.is_placeholder)
				return e | color(theme->toggleOff);
			return e | color(theme->toggleOn);
		};
		numInputOpt.on_change = [&value, &str, minVal, maxVal, onChange] {
			try {
				float v = std::stof(str);
				value = std::clamp(v, minVal, maxVal);
			} catch (...) {
			}
			onChange();
		};
		auto inp = Input(&str, "", numInputOpt);
		return Controls{ minus, inp, plus };
	};

	// Same pattern as makeFloatControls but for double-precision values
	auto makeDoubleControls = [&](double &value,
								  std::string &str,
								  double minVal,
								  double maxVal,
								  double step) {
		struct Controls
		{
			Component minus, input, plus;
		};
		auto minus = Button(
			"-",
			[&value, &str, minVal, step, onChange] {
				value = std::max(minVal, value - step);
				str = ui_utils::formatFloat(value);
				onChange();
			},
			btnStyle);
		auto plus = Button(
			"+",
			[&value, &str, maxVal, step, onChange] {
				value = std::min(maxVal, value + step);
				str = ui_utils::formatFloat(value);
				onChange();
			},
			btnStyle);
		InputOption numInputOpt = inputOpt;
		numInputOpt.transform = [](InputState state) {
			auto theme = ThemeManager::instance().getActive();
			auto e = state.element | center;
			if (state.is_placeholder)
				return e | color(theme->toggleOff);
			return e | color(theme->toggleOn);
		};
		numInputOpt.on_change = [&value, &str, minVal, maxVal, onChange] {
			try {
				double v = std::stod(str);
				value = std::clamp(v, minVal, maxVal);
			} catch (...) {
			}
			onChange();
		};
		auto inp = Input(&str, "", numInputOpt);
		return Controls{ minus, inp, plus };
	};

	// -----------------------------------------------------------------------
	// Load components
	// Creates FTXUI input components for model loading parameters
	// Each component is bound to a member variable and triggers auto-save
	// -----------------------------------------------------------------------
	auto gpuLayersInput = Input(&m_ngpuLayers, "auto", inputOpt);
	auto devicePriorityInput = Input(&m_devicePriority, "e.g. 0", inputOpt);
	auto ctxSizeInput = Input(&m_ctxSize, "0 = default", inputOpt);

	auto [batchSizeMinus, batchSizeInput, batchSizePlus] =
		makeIntControls(m_batchSize, m_batchSizeStr, 32, 8192, 32);

	auto [ubatchSizeMinus, ubatchSizeInput, ubatchSizePlus] =
		makeIntControls(m_ubatchSize, m_ubatchSizeStr, 32, 4096, 32);

	auto [parallelMinus, parallelInput, parallelPlus] =
		makeIntControls(m_parallel, m_parallelStr, -1, 128, 1);

	auto flashAttnToggle =
		ui_utils::makeDropdown(&m_flashAttnOptions, &m_flashAttnIdx, onChange);

	// Split mode dropdown
	auto splitModeToggle =
		ui_utils::makeDropdown(&m_splitModeOptions, &m_splitModeIdx, onChange);

	// Cache type K dropdown
	auto cacheTypeKToggle =
		ui_utils::makeDropdown(&m_cacheTypeOptions, &m_cacheTypeKIdx, onChange);

	// Cache type V dropdown
	auto cacheTypeVToggle =
		ui_utils::makeDropdown(&m_cacheTypeOptions, &m_cacheTypeVIdx, onChange);

	// Reasoning format dropdown
	auto reasoningFormatToggle =
		ui_utils::makeDropdown(&m_reasoningFormatOptions,
							   &m_reasoningFormatIdx,
							   onChange);

	// Spec type dropdown
	auto specTypeToggle =
		ui_utils::makeDropdown(&m_specTypeOptions, &m_specTypeIdx, onChange);

	// Draft cache type dropdowns (reuse m_cacheTypeOptions)
	auto cacheTypeKDraftToggle = ui_utils::makeDropdown(&m_cacheTypeOptions,
														&m_cacheTypeKDraftIdx,
														onChange);

	auto cacheTypeVDraftToggle = ui_utils::makeDropdown(&m_cacheTypeOptions,
														&m_cacheTypeVDraftIdx,
														onChange);

	// Per-GPU tensor split inputs — one Input per detected GPU.
	// m_tensorSplitFields is sized in loadFromConfig() and never resized here;
	// Input holds raw std::string* into the vector — reallocation = UB.
	m_tensorSplitInputs.clear();
	for (size_t i = 0; i < m_tensorSplitFields.size(); ++i)
		m_tensorSplitInputs.push_back(
			Input(&m_tensorSplitFields[i], "1.00", inputOpt));

	// New: Text inputs for new settings
	auto loraInput = Input(&m_lora, "path/to/adapter.gguf", inputOpt);
	auto mmprojInput = Input(&m_mmproj, "path/to/mmproj.gguf", inputOpt);
	auto modelDraftInput = Input(&m_modelDraft, "path/to/draft.gguf", inputOpt);
	auto draftMaxInput = Input(&m_draftMax, "-1 = auto", inputOpt);
	auto deviceDraftInput =
		Input(&m_deviceDraft, "CUDA0,CUDA1 (blank = auto)", inputOpt);
	auto chatTemplateInput = Input(&m_chatTemplate, "e.g. chatml", inputOpt);

	auto kvOffloadCb = Checkbox("", &m_kvOffload, cbOpt);
	auto kvUnifiedCb = Checkbox("", &m_kvUnified, cbOpt);
	auto mmapCb = Checkbox("", &m_mmap, cbOpt);
	auto mlockCb = Checkbox("", &m_mlock, cbOpt);
	auto fitCb = Checkbox("", &m_fit, cbOpt);
	auto preserveThinkingCb = Checkbox("", &m_preserveThinking, cbOpt);

	// Issue #103 section C load-param widgets
	auto [nCpuMoeMinus, nCpuMoeInput, nCpuMoePlus] =
		makeIntControls(m_nCpuMoe, m_nCpuMoeStr, -1, 4096, 1);
	auto cpuMoeCb = Checkbox("", &m_cpuMoe, cbOpt);
	auto overrideTensorInput =
		Input(&m_overrideTensor, "regex=device", inputOpt);
	auto ropeScalingToggle = ui_utils::makeDropdown(&m_ropeScalingOptions,
													&m_ropeScalingIdx,
													onChange);
	auto [ropeScaleMinus, ropeScaleInput, ropeScalePlus] =
		makeDoubleControls(m_ropeScale, m_ropeScaleStr, 0.0, 64.0, 0.1);
	auto [ropeFreqBaseMinus, ropeFreqBaseInput, ropeFreqBasePlus] =
		makeDoubleControls(m_ropeFreqBase, m_ropeFreqBaseStr, 0.0, 1e7, 1000.0);
	auto [ropeFreqScaleMinus, ropeFreqScaleInput, ropeFreqScalePlus] =
		makeDoubleControls(m_ropeFreqScale, m_ropeFreqScaleStr, 0.0, 64.0, 0.1);
	auto [yarnOrigCtxMinus, yarnOrigCtxInput, yarnOrigCtxPlus] =
		makeIntControls(m_yarnOrigCtx, m_yarnOrigCtxStr, 0, 1048576, 512);
	auto [yarnExtMinus, yarnExtInput, yarnExtPlus] =
		makeDoubleControls(m_yarnExtFactor, m_yarnExtFactorStr, -1.0, 1.0, 0.1);
	auto [yarnAttnMinus, yarnAttnInput, yarnAttnPlus] =
		makeDoubleControls(m_yarnAttnFactor, m_yarnAttnFactorStr, 0.0, 8.0, 0.1);
	auto [yarnBetaSlowMinus, yarnBetaSlowInput, yarnBetaSlowPlus] =
		makeDoubleControls(m_yarnBetaSlow, m_yarnBetaSlowStr, 0.0, 64.0, 1.0);
	auto [yarnBetaFastMinus, yarnBetaFastInput, yarnBetaFastPlus] =
		makeDoubleControls(m_yarnBetaFast, m_yarnBetaFastStr, 0.0, 256.0, 1.0);
	auto swaFullCb = Checkbox("", &m_swaFull, cbOpt);
	auto [keepMinus, keepInput, keepPlus] =
		makeIntControls(m_keep, m_keepStr, -1, 1048576, 64);
	auto numaToggle =
		ui_utils::makeDropdown(&m_numaOptions, &m_numaIdx, onChange);
	auto fitTargetToggle =
		ui_utils::makeDropdown(&m_fitTargetOptions, &m_fitTargetIdx, onChange);
	auto [fitCtxMinus, fitCtxInput, fitCtxPlus] =
		makeIntControls(m_fitCtx, m_fitCtxStr, 0, 1048576, 512);
	auto checkTensorsCb = Checkbox("", &m_checkTensors, cbOpt);
	auto overrideKvInput = Input(&m_overrideKv, "KEY=TYPE:VALUE", inputOpt);
	auto loraScaledInput = Input(&m_loraScaled, "FNAME=SCALE", inputOpt);
	auto controlVectorInput =
		Input(&m_controlVector, "path/to/vector.gguf", inputOpt);
	auto controlVectorScaledInput =
		Input(&m_controlVectorScaled, "FNAME=SCALE", inputOpt);
	auto [specDraftNMinMinus, specDraftNMinInput, specDraftNMinPlus] =
		makeIntControls(m_specDraftNMin, m_specDraftNMinStr, -1, 4096, 1);
	auto [specDraftPMinMinus, specDraftPMinInput, specDraftPMinPlus] =
		makeDoubleControls(m_specDraftPMin, m_specDraftPMinStr, -1.0, 1.0, 0.01);
	auto [specDraftPSplitMinus, specDraftPSplitInput, specDraftPSplitPlus] =
		makeDoubleControls(m_specDraftPSplit,
						   m_specDraftPSplitStr,
						   -1.0,
						   1.0,
						   0.01);
	auto cpuMoeDraftCb = Checkbox("", &m_cpuMoeDraft, cbOpt);

	// -----------------------------------------------------------------------
	// Preset components
	// -----------------------------------------------------------------------
	// Refresh presets for the initially selected model
	refreshPresetsForModel();
	// Auto-select + apply the first preset on first load (fixes #72: previously
	// only fired on model change, so first-load model had no preset applied).
	autoSelectFirstPreset();

	auto presetMenuOpt = MenuOption::Vertical();
	presetMenuOpt.on_change = [this] {
		// Always apply preset when clicked (even if same one re-selected)
		if (m_selectedPresetIndex >= 0 &&
			m_selectedPresetIndex < static_cast<int>(m_presetsForModel.size())) {
			applyPreset(m_presetsForModel[m_selectedPresetIndex]);
			m_editingPresetName = m_presetsForModel[m_selectedPresetIndex].name;
			m_presetStatus.clear();
		} else if (!m_presetsForModel.empty()) {
			// Edge case: index out of bounds but presets exist → select first
			m_selectedPresetIndex = 0;
			applyPreset(m_presetsForModel[0]);
			m_editingPresetName = m_presetsForModel[0].name;
			m_presetStatus.clear();
		}
	};
	presetMenuOpt.entries_option.transform = [](const EntryState &s) {
		auto theme = ThemeManager::instance().getActive();
		auto e = text(s.label);
		if (s.active)
			e |= color(theme->toggleOn);
		else
			e |= color(theme->toggleOff);
		if (s.focused)
			e |= bold;
		return e;
	};
	auto presetMenu =
		Menu(&m_presetDisplayNames, &m_selectedPresetIndex, presetMenuOpt);

	InputOption presetNameOpt;
	presetNameOpt.multiline = false;
	presetNameOpt.transform = [](InputState state) {
		auto theme = ThemeManager::instance().getActive();
		auto e = state.element | align_right;
		if (state.is_placeholder)
			return e | color(theme->toggleOff);
		return e | color(theme->toggleOn);
	};
	auto presetNameInput =
		Input(&m_editingPresetName, "preset name", presetNameOpt);

	auto presetBtnStyle = ButtonOption::Animated();
	presetBtnStyle.transform = [](const EntryState &s) {
		auto theme = ThemeManager::instance().getActive();
		auto e = text(s.label) | color(theme->label);
		if (s.focused)
			e |= bold;
		return e | center;
	};

	auto presetSaveBtn =
		Button("Save", [this] { saveCurrentToPreset(); }, presetBtnStyle);

	auto presetRenameBtn = Button(
		"Rename",
		[this] { renameSelectedPreset(m_editingPresetName); },
		presetBtnStyle);

	auto presetNewBtn = Button(
		"+ New",
		[this] {
			m_selectedPresetIndex = -1;
			std::string base =
				m_selectedModelName.empty() ? "preset" : m_selectedModelName;
			m_editingPresetName = base + "-new";
			m_presetStatus.clear();
		},
		presetBtnStyle);

	auto presetDeleteBtn =
		Button("Delete", [this] { deleteSelectedPreset(); }, presetBtnStyle);

	// Opens the "Add New Model" modal. Sits in the preset button row, left of
	// the Name field (see the hbox below). Same styling as the other buttons.
	auto addModelBtn =
		Button("+ Add Model", [this] { openAddModelPopup(); }, presetBtnStyle);

	// -----------------------------------------------------------------------
	// Model Selection Dropdown, START/STOP, and LOAD/UNLOAD Buttons
	// -----------------------------------------------------------------------
	// Model dropdown - selecting a model updates m_modelPath which is used when
	// clicking LOAD/UNLOAD Note: Using Dropdown component for scrollable list
	// (works with 100+ models)
	auto modelDropdown = Dropdown(&m_modelDisplayNames, &m_modelDropdownIndex);

	// Single button that changes based on server state:
	// - Server not running: shows "LOAD" (green) - starts the server
	// - Server running, no model: shows "LOAD" (green) - loads selected model
	// via API
	// - Server running, model loaded: shows "UNLOAD" (red) - unloads current
	// model
	auto serverButton = Button(
		&m_startStopLabel,
		[this] {
			if (!m_server.isRunning()) {
				// Server not running - START it
				onStartStopClicked();
			} else {
				// Server is running - LOAD or UNLOAD model
				onLoadUnloadClicked();
			}
		},
		startStopBtnStyle);

	// -----------------------------------------------------------------------
	// Inference components
	// Creates controls for text generation parameters (temperature, sampling,
	// penalties) Float controls use 0.01 step for fine-grained probability
	// adjustments
	// -----------------------------------------------------------------------
	auto [tempMinus, tempInput, tempPlus] =
		makeFloatControls(m_temperature, m_temperatureStr, 0.0f, 2.0f, 0.01f);
	auto [topPMinus, topPInput, topPPlus] =
		makeFloatControls(m_topP, m_topPStr, 0.0f, 1.0f, 0.01f);
	auto [topKMinus, topKInput, topKPlus] =
		makeIntControls(m_topK, m_topKStr, 0, 200, 1);
	auto [minPMinus, minPInput, minPPlus] =
		makeFloatControls(m_minP, m_minPStr, 0.0f, 1.0f, 0.01f);
	auto [repeatPenMinus, repeatPenInput, repeatPenPlus] =
		makeFloatControls(m_repeatPenalty,
						  m_repeatPenaltyStr,
						  1.0f,
						  2.0f,
						  0.01f);
	auto [presPenMinus, presPenInput, presPenPlus] =
		makeFloatControls(m_presencePenalty,
						  m_presencePenaltyStr,
						  -2.0f,
						  2.0f,
						  0.01f);
	auto [freqPenMinus, freqPenInput, freqPenPlus] =
		makeFloatControls(m_frequencyPenalty,
						  m_frequencyPenaltyStr,
						  -2.0f,
						  2.0f,
						  0.01f);

	auto nPredictInput = Input(&m_nPredict, "-1 = unlimited", inputOpt);
	auto seedInput = Input(&m_seed, "-1 = random", inputOpt);

	// Phase 4 inference params widgets
	auto ignoreEosCb = Checkbox("", &m_ignoreEos);
	auto logitBiasInput = Input(&m_logitBias, "TOKEN_ID(+/-)BIAS", inputOpt);
	auto [adaptiveTargetMinus, adaptiveTargetInput, adaptiveTargetPlus] =
		makeDoubleControls(m_adaptiveTarget,
						   m_adaptiveTargetStr,
						   0.0,
						   1024.0,
						   0.01);
	auto [adaptiveDecayMinus, adaptiveDecayInput, adaptiveDecayPlus] =
		makeDoubleControls(m_adaptiveDecay, m_adaptiveDecayStr, 0.0, 1.0, 0.01);
	auto grammarFileInput =
		Input(&m_grammarFile, "path to .gbnf file", inputOpt);
	auto jsonSchemaFileInput =
		Input(&m_jsonSchemaFile, "path to .json schema file", inputOpt);
	auto samplerSeqInput =
		Input(&m_samplerSeq, "e.g. mp (min-p, penalties)", inputOpt);
	auto drySequenceBreakerInput =
		Input(&m_drySequenceBreaker, "sequence breaker token", inputOpt);
	auto backendSamplingCb = Checkbox("", &m_backendSampling);

	// -----------------------------------------------------------------------
	// Container — two-column layout: Load Settings (left), Inference (right)
	// Uses FTXUI's Container::Horizontal to split the panel vertically
	// Each column is a vertical stack of interactive components
	// The Renderer below wraps each column in a labeled window
	// -----------------------------------------------------------------------
	// Build left column components dynamically to accommodate variable number of
	// per-GPU tensor split inputs.
	Components leftColComponents = {
		gpuLayersInput,	 devicePriorityInput, ctxSizeInput,	   batchSizeMinus,
		batchSizeInput,	 batchSizePlus,		  ubatchSizeMinus, ubatchSizeInput,
		ubatchSizePlus,	 parallelMinus,		  parallelInput,   parallelPlus,
		flashAttnToggle, splitModeToggle,
	};
	// Insert per-GPU tensor split inputs (only when >1 GPU)
	if (m_tensorSplitInputs.size() > 1) {
		for (auto &inp : m_tensorSplitInputs)
			leftColComponents.push_back(inp);
	}
	leftColComponents.insert(
		leftColComponents.end(),
		{
			cacheTypeKToggle,
			cacheTypeVToggle,
			loraInput,
			mmprojInput,
			modelDraftInput,
			draftMaxInput,
			chatTemplateInput,
			reasoningFormatToggle,
			specTypeToggle,
			cacheTypeKDraftToggle,
			cacheTypeVDraftToggle,
			deviceDraftInput,
			kvOffloadCb,
			kvUnifiedCb,
			mmapCb,
			mlockCb,
			fitCb,
			preserveThinkingCb,
			// Issue #103 section C
			nCpuMoeMinus,
			nCpuMoeInput,
			nCpuMoePlus,
			cpuMoeCb,
			overrideTensorInput,
			ropeScalingToggle,
			ropeScaleMinus,
			ropeScaleInput,
			ropeScalePlus,
			ropeFreqBaseMinus,
			ropeFreqBaseInput,
			ropeFreqBasePlus,
			ropeFreqScaleMinus,
			ropeFreqScaleInput,
			ropeFreqScalePlus,
			yarnOrigCtxMinus,
			yarnOrigCtxInput,
			yarnOrigCtxPlus,
			yarnExtMinus,
			yarnExtInput,
			yarnExtPlus,
			yarnAttnMinus,
			yarnAttnInput,
			yarnAttnPlus,
			yarnBetaSlowMinus,
			yarnBetaSlowInput,
			yarnBetaSlowPlus,
			yarnBetaFastMinus,
			yarnBetaFastInput,
			yarnBetaFastPlus,
			swaFullCb,
			keepMinus,
			keepInput,
			keepPlus,
			numaToggle,
			fitTargetToggle,
			fitCtxMinus,
			fitCtxInput,
			fitCtxPlus,
			checkTensorsCb,
			overrideKvInput,
			loraScaledInput,
			controlVectorInput,
			controlVectorScaledInput,
			specDraftNMinMinus,
			specDraftNMinInput,
			specDraftNMinPlus,
			specDraftPMinMinus,
			specDraftPMinInput,
			specDraftPMinPlus,
			specDraftPSplitMinus,
			specDraftPSplitInput,
			specDraftPSplitPlus,
			cpuMoeDraftCb,
			// Presets
			presetMenu,
			addModelBtn,
			presetNameInput,
			presetSaveBtn,
			presetRenameBtn,
			presetNewBtn,
			presetDeleteBtn,
			// Footer: model dropdown and single server button
			modelDropdown,
			serverButton,
		});

	Components rightColComponents = {
		tempMinus,
		tempInput,
		tempPlus,
		topPMinus,
		topPInput,
		topPPlus,
		topKMinus,
		topKInput,
		topKPlus,
		minPMinus,
		minPInput,
		minPPlus,
		repeatPenMinus,
		repeatPenInput,
		repeatPenPlus,
		presPenMinus,
		presPenInput,
		presPenPlus,
		freqPenMinus,
		freqPenInput,
		freqPenPlus,
		nPredictInput,
		seedInput,
		ignoreEosCb,
		logitBiasInput,
		adaptiveTargetMinus,
		adaptiveTargetInput,
		adaptiveTargetPlus,
		adaptiveDecayMinus,
		adaptiveDecayInput,
		adaptiveDecayPlus,
		grammarFileInput,
		jsonSchemaFileInput,
		samplerSeqInput,
		drySequenceBreakerInput,
		backendSamplingCb,
	};

	auto container = Container::Horizontal({
		Container::Vertical(leftColComponents),
		Container::Vertical(rightColComponents),
	});

	// -----------------------------------------------------------------------
	// Add-New-Model popup (ftxui::Modal). Shown over the panel when
	// m_showAddModelPopup is true. Save validates + writes a default preset;
	// Cancel closes without writing. Both buttons close the modal.
	// -----------------------------------------------------------------------
	auto addModelPathInput =
		Input(&m_addModelPath, "full path to .gguf file", inputOpt);
	auto addModelSaveBtn =
		Button("Save", [this] { confirmAddModel(); }, presetBtnStyle);
	auto addModelCancelBtn =
		Button("Cancel", [this] { cancelAddModel(); }, presetBtnStyle);

	auto addModelPopupContainer = Container::Vertical({
		addModelPathInput,
		Container::Horizontal({ addModelSaveBtn, addModelCancelBtn }),
	});

	auto addModelPopup = Renderer(addModelPopupContainer, [=, this] {
		auto theme = ThemeManager::instance().getActive();
		Elements rows;
		rows.push_back(
			ui_utils::settingRowComponent("GGUF Path",
										  addModelPathInput->Render()));
		if (!m_addModelError.empty())
			rows.push_back(text("  " + m_addModelError) | color(theme->error));
		rows.push_back(separatorLight());
		rows.push_back(hbox({
			filler(),
			addModelSaveBtn->Render(),
			separatorEmpty(),
			addModelCancelBtn->Render(),
		}));
		return window(text("Add New Model") | bold | color(theme->title),
					  vbox(std::move(rows))) |
			   size(WIDTH, GREATER_THAN, 50) | clear_under;
	});

	// Track model dropdown changes to refresh presets
	int lastModelIndex = m_modelDropdownIndex;
	// Track preset-Menu selection too, so the LOAD/UNLOAD label flips the frame
	// the user highlights a different preset (#110), not on the next 2 s tick.
	int lastPresetIndex = m_selectedPresetIndex;
	// Periodic refresh counter - refresh every ~2 seconds (60 renders at 30Hz)
	int renderCount = 0;
	auto lastRefreshTime = std::chrono::steady_clock::now();
	// Tracks whether an async start/load worker was in flight last frame, so
	// the completion edge can refresh state immediately on the UI thread.
	bool lastAsyncBusy = false;

	m_component = Renderer(container, [=, this]() mutable {
		// Re-read the active theme each frame so a live switch is reflected.
		auto theme = ThemeManager::instance().getActive();

		// Async worker completion edge: the worker only flips its atomic; the
		// label and model state are refreshed here, on the UI thread, the
		// frame the flag drops (instead of waiting for the 2 s tick below).
		bool asyncBusy = m_serverStarting.load(std::memory_order_acquire) ||
						 m_modelLoading.load(std::memory_order_acquire);
		if (lastAsyncBusy && !asyncBusy) {
			refreshServerState();
			updateStartStopLabel();
		}
		lastAsyncBusy = asyncBusy;

		// Periodic server state refresh to keep button state accurate
		renderCount++;
		auto now = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
						   now - lastRefreshTime)
						   .count();
		if (elapsed >= 2) { // Refresh every 2 seconds
			lastRefreshTime = now;
			refreshServerState();
			updateStartStopLabel();
		}

		// Detect model dropdown change
		if (m_modelDropdownIndex != lastModelIndex) {
			lastModelIndex = m_modelDropdownIndex;
			if (m_modelDropdownIndex >= 0 &&
				m_modelDropdownIndex < static_cast<int>(m_modelNames.size())) {
				m_selectedModelName = m_modelNames[m_modelDropdownIndex];
				m_modelPath = m_modelPaths[m_modelDropdownIndex];
			}
			refreshPresetsForModel();
			autoSelectFirstPreset();
			lastPresetIndex = m_selectedPresetIndex;
			updateStartStopLabel();
		}

		// Detect preset-Menu selection change (same GGUF, different preset): the
		// load/unload target changed, so refresh the button label.
		if (m_selectedPresetIndex != lastPresetIndex) {
			lastPresetIndex = m_selectedPresetIndex;
			updateStartStopLabel();
		}

		// === Left column: Load Settings ===
		Elements leftElements;
		{
			Elements rows;
			rows.push_back(
				ui_utils::settingRowComponent("GPU Layers",
											  gpuLayersInput->Render(),
											  &m_ngpuLayers));
			rows.push_back(
				ui_utils::settingRowComponent("Device Priority",
											  devicePriorityInput->Render(),
											  &m_devicePriority));
			rows.push_back(ui_utils::settingRowComponent("Context Size",
														 ctxSizeInput->Render(),
														 &m_ctxSize));
			rows.push_back(ui_utils::numberRow("Batch Size",
											   batchSizeMinus->Render(),
											   batchSizeInput->Render(),
											   batchSizePlus->Render(),
											   m_batchSizeStr));
			rows.push_back(ui_utils::numberRow("UBatch Size",
											   ubatchSizeMinus->Render(),
											   ubatchSizeInput->Render(),
											   ubatchSizePlus->Render(),
											   m_ubatchSizeStr));
			rows.push_back(ui_utils::numberRow("Parallel Slots",
											   parallelMinus->Render(),
											   parallelInput->Render(),
											   parallelPlus->Render(),
											   m_parallelStr));
			rows.push_back(ui_utils::checkboxRow("Flash Attention",
												 flashAttnToggle->Render()));
			rows.push_back(
				ui_utils::checkboxRow("Split Mode", splitModeToggle->Render()));
			// Only render tensor split UI when more than one GPU is present.
			// Single-GPU systems don't benefit from split configuration — hide
			// entirely.
			if (m_tensorSplitInputs.size() > 1) {
				rows.push_back(
					ui_utils::settingRowComponent("Tensor Split", text("")));
				for (size_t i = 0; i < m_tensorSplitInputs.size(); ++i) {
					rows.push_back(ui_utils::settingRowComponent(
						"  GPU " + std::to_string(i),
						m_tensorSplitInputs[i]->Render()));
				}
			}
			rows.push_back(ui_utils::checkboxRow("Cache Type K",
												 cacheTypeKToggle->Render()));
			rows.push_back(ui_utils::checkboxRow("Cache Type V",
												 cacheTypeVToggle->Render()));
			rows.push_back(ui_utils::settingRowComponent("LoRA",
														 loraInput->Render(),
														 &m_lora));
			rows.push_back(ui_utils::settingRowComponent("MM Projector",
														 mmprojInput->Render(),
														 &m_mmproj));
			rows.push_back(
				ui_utils::settingRowComponent("Draft Model",
											  modelDraftInput->Render(),
											  &m_modelDraft));
			rows.push_back(ui_utils::settingRowComponent("Draft Max",
														 draftMaxInput->Render(),
														 &m_draftMax));
			rows.push_back(
				ui_utils::settingRowComponent("Chat Template",
											  chatTemplateInput->Render(),
											  &m_chatTemplate));
			rows.push_back(
				ui_utils::checkboxRow("Reasoning Format",
									  reasoningFormatToggle->Render()));
			rows.push_back(
				ui_utils::checkboxRow("Spec Type", specTypeToggle->Render()));
			rows.push_back(
				ui_utils::checkboxRow("Draft Cache K",
									  cacheTypeKDraftToggle->Render()));
			rows.push_back(
				ui_utils::checkboxRow("Draft Cache V",
									  cacheTypeVDraftToggle->Render()));
			rows.push_back(
				ui_utils::settingRowComponent("Draft Device",
											  deviceDraftInput->Render(),
											  &m_deviceDraft));
			rows.push_back(ui_utils::checkboxRow("KV Cache Offload",
												 kvOffloadCb->Render()));
			rows.push_back(
				ui_utils::checkboxRow("KV Unified", kvUnifiedCb->Render()));
			rows.push_back(
				ui_utils::checkboxRow("Memory Map", mmapCb->Render()));
			rows.push_back(
				ui_utils::checkboxRow("Memory Lock", mlockCb->Render()));
			rows.push_back(
				ui_utils::checkboxRow("Fit to Memory", fitCb->Render()));
			rows.push_back(ui_utils::checkboxRow("Preserve Thinking",
												 preserveThinkingCb->Render()));
			// Issue #103 section C
			rows.push_back(ui_utils::numberRow("CPU MoE Layers",
											   nCpuMoeMinus->Render(),
											   nCpuMoeInput->Render(),
											   nCpuMoePlus->Render(),
											   m_nCpuMoeStr));
			rows.push_back(ui_utils::checkboxRow("CPU MoE", cpuMoeCb->Render()));
			rows.push_back(
				ui_utils::settingRowComponent("Override Tensor",
											  overrideTensorInput->Render(),
											  &m_overrideTensor));
			rows.push_back(ui_utils::checkboxRow("RoPE Scaling",
												 ropeScalingToggle->Render()));
			rows.push_back(ui_utils::numberRow("RoPE Scale",
											   ropeScaleMinus->Render(),
											   ropeScaleInput->Render(),
											   ropeScalePlus->Render(),
											   m_ropeScaleStr));
			rows.push_back(ui_utils::numberRow("RoPE Freq Base",
											   ropeFreqBaseMinus->Render(),
											   ropeFreqBaseInput->Render(),
											   ropeFreqBasePlus->Render(),
											   m_ropeFreqBaseStr));
			rows.push_back(ui_utils::numberRow("RoPE Freq Scale",
											   ropeFreqScaleMinus->Render(),
											   ropeFreqScaleInput->Render(),
											   ropeFreqScalePlus->Render(),
											   m_ropeFreqScaleStr));
			rows.push_back(ui_utils::numberRow("YaRN Orig Ctx",
											   yarnOrigCtxMinus->Render(),
											   yarnOrigCtxInput->Render(),
											   yarnOrigCtxPlus->Render(),
											   m_yarnOrigCtxStr));
			rows.push_back(ui_utils::numberRow("YaRN Ext Factor",
											   yarnExtMinus->Render(),
											   yarnExtInput->Render(),
											   yarnExtPlus->Render(),
											   m_yarnExtFactorStr));
			rows.push_back(ui_utils::numberRow("YaRN Attn Factor",
											   yarnAttnMinus->Render(),
											   yarnAttnInput->Render(),
											   yarnAttnPlus->Render(),
											   m_yarnAttnFactorStr));
			rows.push_back(ui_utils::numberRow("YaRN Beta Slow",
											   yarnBetaSlowMinus->Render(),
											   yarnBetaSlowInput->Render(),
											   yarnBetaSlowPlus->Render(),
											   m_yarnBetaSlowStr));
			rows.push_back(ui_utils::numberRow("YaRN Beta Fast",
											   yarnBetaFastMinus->Render(),
											   yarnBetaFastInput->Render(),
											   yarnBetaFastPlus->Render(),
											   m_yarnBetaFastStr));
			rows.push_back(
				ui_utils::checkboxRow("SWA Full", swaFullCb->Render()));
			rows.push_back(ui_utils::numberRow("Keep Tokens",
											   keepMinus->Render(),
											   keepInput->Render(),
											   keepPlus->Render(),
											   m_keepStr));
			rows.push_back(ui_utils::checkboxRow("NUMA", numaToggle->Render()));
			rows.push_back(
				ui_utils::checkboxRow("Fit Target", fitTargetToggle->Render()));
			rows.push_back(ui_utils::numberRow("Fit Ctx",
											   fitCtxMinus->Render(),
											   fitCtxInput->Render(),
											   fitCtxPlus->Render(),
											   m_fitCtxStr));
			rows.push_back(ui_utils::checkboxRow("Check Tensors",
												 checkTensorsCb->Render()));
			rows.push_back(
				ui_utils::settingRowComponent("Override KV",
											  overrideKvInput->Render(),
											  &m_overrideKv));
			rows.push_back(
				ui_utils::settingRowComponent("LoRA Scaled",
											  loraScaledInput->Render(),
											  &m_loraScaled));
			rows.push_back(
				ui_utils::settingRowComponent("Control Vector",
											  controlVectorInput->Render(),
											  &m_controlVector));
			rows.push_back(
				ui_utils::settingRowComponent("Control Vector Scaled",
											  controlVectorScaledInput->Render(),
											  &m_controlVectorScaled));
			rows.push_back(ui_utils::numberRow("Spec Draft N Min",
											   specDraftNMinMinus->Render(),
											   specDraftNMinInput->Render(),
											   specDraftNMinPlus->Render(),
											   m_specDraftNMinStr));
			rows.push_back(ui_utils::numberRow("Spec Draft P Min",
											   specDraftPMinMinus->Render(),
											   specDraftPMinInput->Render(),
											   specDraftPMinPlus->Render(),
											   m_specDraftPMinStr));
			rows.push_back(ui_utils::numberRow("Spec Draft P Split",
											   specDraftPSplitMinus->Render(),
											   specDraftPSplitInput->Render(),
											   specDraftPSplitPlus->Render(),
											   m_specDraftPSplitStr));
			rows.push_back(
				ui_utils::checkboxRow("CPU MoE Draft", cpuMoeDraftCb->Render()));
			leftElements.push_back(window(
				text("Load Settings") | bold | color(theme->title),
				hbox({ text("    "),
					   vbox(std::move(rows)) | xflex | vscroll_indicator |
						   yframe |
						   size(HEIGHT, LESS_THAN, LOAD_SETTINGS_MAX_LINES) }),
				ftxui::EMPTY));
		}

		// === Right column: Inference Settings ===
		Elements rightElements;
		{
			Elements rows;
			rows.push_back(ui_utils::numberRow("Temperature",
											   tempMinus->Render(),
											   tempInput->Render(),
											   tempPlus->Render(),
											   m_temperatureStr));
			rows.push_back(ui_utils::numberRow("Top P",
											   topPMinus->Render(),
											   topPInput->Render(),
											   topPPlus->Render(),
											   m_topPStr));
			rows.push_back(ui_utils::numberRow("Top K",
											   topKMinus->Render(),
											   topKInput->Render(),
											   topKPlus->Render(),
											   m_topKStr));
			rows.push_back(ui_utils::numberRow("Min P",
											   minPMinus->Render(),
											   minPInput->Render(),
											   minPPlus->Render(),
											   m_minPStr));
			rows.push_back(ui_utils::numberRow("Repeat Penalty",
											   repeatPenMinus->Render(),
											   repeatPenInput->Render(),
											   repeatPenPlus->Render(),
											   m_repeatPenaltyStr));
			rows.push_back(ui_utils::numberRow("Presence Penalty",
											   presPenMinus->Render(),
											   presPenInput->Render(),
											   presPenPlus->Render(),
											   m_presencePenaltyStr));
			rows.push_back(ui_utils::numberRow("Frequency Penalty",
											   freqPenMinus->Render(),
											   freqPenInput->Render(),
											   freqPenPlus->Render(),
											   m_frequencyPenaltyStr));
			rows.push_back(ui_utils::settingRowComponent("Max Tokens",
														 nPredictInput->Render(),
														 &m_nPredict));
			rows.push_back(ui_utils::settingRowComponent("Seed",
														 seedInput->Render(),
														 &m_seed));

			// Phase 4 inference params rows
			rows.push_back(
				ui_utils::checkboxRow("Ignore EOS", ignoreEosCb->Render()));
			rows.push_back(
				ui_utils::settingRowComponent("Logit Bias",
											  logitBiasInput->Render(),
											  &m_logitBias));
			rows.push_back(ui_utils::numberRow("Adaptive Target",
											   adaptiveTargetMinus->Render(),
											   adaptiveTargetInput->Render(),
											   adaptiveTargetPlus->Render(),
											   m_adaptiveTargetStr));
			rows.push_back(ui_utils::numberRow("Adaptive Decay",
											   adaptiveDecayMinus->Render(),
											   adaptiveDecayInput->Render(),
											   adaptiveDecayPlus->Render(),
											   m_adaptiveDecayStr));
			rows.push_back(
				ui_utils::settingRowComponent("JSON Schema File",
											  jsonSchemaFileInput->Render(),
											  &m_jsonSchemaFile));
			rows.push_back(
				ui_utils::settingRowComponent("Grammar File",
											  grammarFileInput->Render(),
											  &m_grammarFile));
			rows.push_back(
				ui_utils::settingRowComponent("Sampler Sequence",
											  samplerSeqInput->Render(),
											  &m_samplerSeq));
			rows.push_back(
				ui_utils::settingRowComponent("DRY Sequence Breaker",
											  drySequenceBreakerInput->Render(),
											  &m_drySequenceBreaker));
			rows.push_back(ui_utils::checkboxRow("Backend Sampling",
												 backendSamplingCb->Render()));

			rightElements.push_back(
				window(text("Inference Settings") | bold | color(theme->title),
					   hbox({ text("    "),
							  vbox(std::move(rows)) | xflex | vscroll_indicator |
								  yframe |
								  size(HEIGHT,
									   LESS_THAN,
									   INFERENCE_SETTINGS_MAX_LINES) }),
					   ftxui::EMPTY));
		}

		auto leftCol = vbox(std::move(leftElements)) | flex;
		auto rightCol = vbox(std::move(rightElements)) | flex;

		// === Presets panel ===
		Elements presetElements;
		if (m_presetDisplayNames.empty()) {
			presetElements.push_back(
				text("  No presets — use Save to create one.") |
				color(theme->mutedText));
		} else {
			presetElements.push_back(presetMenu->Render() | vscroll_indicator |
									 frame | size(HEIGHT, LESS_THAN, 6));
		}
		// Name input + buttons row
		presetElements.push_back(separatorLight());
		presetElements.push_back(hbox({
			addModelBtn->Render(),
			separatorLight(),
			text(" Name: ") | color(theme->subtleLabel),
			presetNameInput->Render() | flex,
			separatorLight(),
			presetRenameBtn->Render(),
			separatorLight(),
			presetSaveBtn->Render(),
			separatorLight(),
			presetNewBtn->Render(),
			separatorLight(),
			presetDeleteBtn->Render(),
		}));
		// Status message + model dropdown + LOAD/UNLOAD button row.
		// Layout: status text (left) | flexible gap | dropdown | gap | button
		// | right margin. filler() absorbs the slack between the status text
		// and the dropdown; separatorEmpty() inserts blank gaps so the
		// dropdown, button, and panel edge do not touch.
		Element statusElem = emptyElement();
		if (!m_presetStatus.empty()) {
			Color statusColor = (m_presetStatus == "Name in use" ||
								 m_presetStatus == "Save failed" ||
								 m_presetStatus == "Rename failed")
									? theme->error
									: theme->success;
			statusElem = text("  " + m_presetStatus) | color(statusColor);
		}
		presetElements.push_back(separatorLight());
		presetElements.push_back(hbox({
			statusElem,
			filler(),
			modelDropdown->Render(),
			separatorEmpty(),
			serverButton->Render(),
			filler(),
		}));

		auto presetsPanel = window(text("Presets") | bold | color(theme->title),
								   vbox(std::move(presetElements)),
								   ftxui::EMPTY);

		return vbox({
				   hbox({ leftCol, separatorLight(), rightCol }),
				   filler(),
				   presetsPanel,
			   }) |
			   xflex | yflex;
	});

	// Overlay the Add-New-Model popup on top of the panel when shown.
	m_component = Modal(m_component, addModelPopup, &m_showAddModelPopup);

	// Esc closes the popup while it is open. Mouse wheel is translated into
	// focus movement so the yframe-scrolled sections respond to the wheel: the
	// render-only yframe tracks the focused control, so moving focus with the
	// wheel scrolls whichever section currently holds focus.
	m_component = m_component | CatchEvent([this, container](Event event) {
					  if (m_showAddModelPopup && event == Event::Escape) {
						  cancelAddModel();
						  return true;
					  }
					  if (event.is_mouse()) {
						  const auto &m = event.mouse();
						  if (m.button == Mouse::WheelDown) {
							  container->OnEvent(Event::ArrowDown);
							  return true;
						  }
						  if (m.button == Mouse::WheelUp) {
							  container->OnEvent(Event::ArrowUp);
							  return true;
						  }
					  }
					  return false;
				  });

	return m_component;
}

// =========================================================================
// Preset Methods
// =========================================================================

void ModelsPanel::refreshPresetsForModel()
{
	if (m_selectedModelName.empty()) {
		m_presetsForModel.clear();
		m_presetDisplayNames.clear();
		return;
	}
	// Find the model path from our parallel vector
	std::string modelPath;
	for (size_t i = 0; i < m_modelNames.size(); ++i) {
		if (m_modelNames[i] == m_selectedModelName) {
			modelPath = m_modelPaths[i];
			break;
		}
	}
	if (modelPath.empty())
		modelPath = m_modelsIni.getModelPath(m_selectedModelName);
	m_presetsForModel = m_modelsIni.getPresetsForModel(modelPath);
	m_presetDisplayNames.clear();
	for (const auto &p : m_presetsForModel)
		m_presetDisplayNames.push_back(p.name);

	// Clamp selected index
	if (m_selectedPresetIndex >= static_cast<int>(m_presetsForModel.size()))
		m_selectedPresetIndex = -1;
}

void ModelsPanel::autoSelectFirstPreset()
{
	if (!m_presetsForModel.empty()) {
		m_selectedPresetIndex = 0;
		applyPreset(m_presetsForModel[0]);
		m_editingPresetName = m_presetsForModel[0].name;
	} else {
		m_selectedPresetIndex = -1;
		m_editingPresetName.clear();
	}
	m_presetStatus.clear();
}

void ModelsPanel::applyPreset(const Config::ModelPreset &preset)
{
	// Load settings
	m_ngpuLayers = preset.load.ngpuLayers < 0
					   ? "all"
					   : std::to_string(preset.load.ngpuLayers);
	m_ctxSize =
		preset.load.ctxSize == 0 ? "" : std::to_string(preset.load.ctxSize);
	m_batchSize = preset.load.batchSize;
	m_batchSizeStr = std::to_string(m_batchSize);
	m_ubatchSize = preset.load.ubatchSize;
	m_ubatchSizeStr = std::to_string(m_ubatchSize);
	m_parallel = preset.load.parallel;
	m_parallelStr = m_parallel < 0 ? "-1" : std::to_string(m_parallel);

	// Flash attention dropdown index
	if (preset.load.flashAttn == "on")
		m_flashAttnIdx = 1;
	else if (preset.load.flashAttn == "off")
		m_flashAttnIdx = 2;
	else
		m_flashAttnIdx = 0;

	m_kvOffload = preset.load.kvOffload;
	m_kvUnified = preset.load.kvUnified;
	m_mmap = preset.load.mmap;
	m_mlock = preset.load.mlock;
	m_fit = preset.load.fit;
	m_devicePriority = preset.load.devicePriority;
	// gpuCount is already fixed — m_tensorSplitFields.size() is authoritative.
	// initTensorSplitFromConfig mutates in-place, never resizes.
	initTensorSplitFromConfig(preset.load.tensorSplit,
							  static_cast<int>(m_tensorSplitFields.size()));
	m_cacheTypeK = preset.load.cacheTypeK;
	m_cacheTypeV = preset.load.cacheTypeV;
	m_lora = preset.load.lora;
	m_mmproj = preset.load.mmproj;
	m_modelDraft = preset.load.modelDraft;
	m_draftMax = std::to_string(preset.load.draftMax);
	m_cacheTypeKDraft = preset.load.cacheTypeKDraft;
	m_cacheTypeVDraft = preset.load.cacheTypeVDraft;
	m_deviceDraft = preset.load.deviceDraft;
	m_preserveThinking = preset.load.preserveThinking;
	m_chatTemplate = preset.load.chatTemplate;
	m_reasoningFormat = preset.load.reasoningFormat;

	// Dropdown indices for load settings
	m_splitModeIdx = findOptionIndex(m_splitModeOptions, preset.load.splitMode);
	m_cacheTypeKIdx =
		findOptionIndex(m_cacheTypeOptions, preset.load.cacheTypeK);
	m_cacheTypeVIdx =
		findOptionIndex(m_cacheTypeOptions, preset.load.cacheTypeV);
	m_reasoningFormatIdx =
		findOptionIndex(m_reasoningFormatOptions, preset.load.reasoningFormat);
	m_specTypeIdx = findOptionIndex(
		m_specTypeOptions,
		preset.load.specType.empty() ? "none" : preset.load.specType);
	m_cacheTypeKDraftIdx =
		findOptionIndex(m_cacheTypeOptions, preset.load.cacheTypeKDraft);
	m_cacheTypeVDraftIdx =
		findOptionIndex(m_cacheTypeOptions, preset.load.cacheTypeVDraft);

	// Issue #103 section C
	m_nCpuMoe = preset.load.nCpuMoe;
	m_nCpuMoeStr = std::to_string(m_nCpuMoe);
	m_cpuMoe = preset.load.cpuMoe;
	m_overrideTensor = preset.load.overrideTensor;
	// Enum index: "" maps to option 0 ("(default)"); findOptionIndex returns 0
	// for an unmatched value, which is exactly the "(default)" slot.
	m_ropeScalingIdx = findOptionIndex(
		m_ropeScalingOptions,
		preset.load.ropeScaling.empty() ? "(default)" : preset.load.ropeScaling);
	m_ropeScale = preset.load.ropeScale;
	m_ropeScaleStr = ui_utils::formatFloat(m_ropeScale);
	m_ropeFreqBase = preset.load.ropeFreqBase;
	m_ropeFreqBaseStr = ui_utils::formatFloat(m_ropeFreqBase);
	m_ropeFreqScale = preset.load.ropeFreqScale;
	m_ropeFreqScaleStr = ui_utils::formatFloat(m_ropeFreqScale);
	m_yarnOrigCtx = preset.load.yarnOrigCtx;
	m_yarnOrigCtxStr = std::to_string(m_yarnOrigCtx);
	m_yarnExtFactor = preset.load.yarnExtFactor;
	m_yarnExtFactorStr = ui_utils::formatFloat(m_yarnExtFactor);
	m_yarnAttnFactor = preset.load.yarnAttnFactor;
	m_yarnAttnFactorStr = ui_utils::formatFloat(m_yarnAttnFactor);
	m_yarnBetaSlow = preset.load.yarnBetaSlow;
	m_yarnBetaSlowStr = ui_utils::formatFloat(m_yarnBetaSlow);
	m_yarnBetaFast = preset.load.yarnBetaFast;
	m_yarnBetaFastStr = ui_utils::formatFloat(m_yarnBetaFast);
	m_swaFull = preset.load.swaFull;
	m_keep = preset.load.keep;
	m_keepStr = std::to_string(m_keep);
	m_numaIdx = findOptionIndex(m_numaOptions,
								preset.load.numa.empty() ? "(default)"
														 : preset.load.numa);
	m_fitTargetIdx = findOptionIndex(
		m_fitTargetOptions,
		preset.load.fitTarget.empty() ? "(default)" : preset.load.fitTarget);
	m_fitCtx = preset.load.fitCtx;
	m_fitCtxStr = std::to_string(m_fitCtx);
	m_checkTensors = preset.load.checkTensors;
	m_overrideKv = preset.load.overrideKv;
	m_loraScaled = preset.load.loraScaled;
	m_controlVector = preset.load.controlVector;
	m_controlVectorScaled = preset.load.controlVectorScaled;
	m_specDraftNMin = preset.load.specDraftNMin;
	m_specDraftNMinStr = std::to_string(m_specDraftNMin);
	m_specDraftPMin = preset.load.specDraftPMin;
	m_specDraftPMinStr = ui_utils::formatFloat(m_specDraftPMin);
	m_specDraftPSplit = preset.load.specDraftPSplit;
	m_specDraftPSplitStr = ui_utils::formatFloat(m_specDraftPSplit);
	m_cpuMoeDraft = preset.load.cpuMoeDraft;

	// Inference settings
	m_temperature = static_cast<float>(preset.inference.temperature);
	m_temperatureStr = ui_utils::formatFloat(m_temperature);
	m_topP = static_cast<float>(preset.inference.topP);
	m_topPStr = ui_utils::formatFloat(m_topP);
	m_topK = preset.inference.topK;
	m_topKStr = std::to_string(m_topK);
	m_minP = static_cast<float>(preset.inference.minP);
	m_minPStr = ui_utils::formatFloat(m_minP);
	m_repeatPenalty = static_cast<float>(preset.inference.repeatPenalty);
	m_repeatPenaltyStr = ui_utils::formatFloat(m_repeatPenalty);
	m_presencePenalty = static_cast<float>(preset.inference.presencePenalty);
	m_presencePenaltyStr = ui_utils::formatFloat(m_presencePenalty);
	m_frequencyPenalty = static_cast<float>(preset.inference.frequencyPenalty);
	m_frequencyPenaltyStr = ui_utils::formatFloat(m_frequencyPenalty);
	m_nPredict = std::to_string(preset.inference.nPredict);
	m_seed = std::to_string(preset.inference.seed);

	// Phase 4 inference params
	m_ignoreEos = preset.inference.ignoreEos;
	m_logitBias = preset.inference.logitBias;
	m_adaptiveTarget = static_cast<float>(preset.inference.adaptiveTarget);
	m_adaptiveTargetStr = ui_utils::formatFloat(m_adaptiveTarget);
	m_adaptiveDecay = static_cast<float>(preset.inference.adaptiveDecay);
	m_adaptiveDecayStr = ui_utils::formatFloat(m_adaptiveDecay);
	m_grammarFile = preset.inference.grammarFile;
	m_jsonSchemaFile = preset.inference.jsonSchemaFile;
	m_samplerSeq = preset.inference.samplerSeq;
	m_drySequenceBreaker = preset.inference.drySequenceBreaker;
	m_backendSampling = preset.inference.backendSampling;

	// Applying a preset only populates the form fields. Persisting happens via
	// saveCurrentToPreset() when the user explicitly saves; the loaded model
	// reads its settings from models.ini through the load API, not config.json.

	spdlog::info("Applied preset '{}'", preset.name);
}

void ModelsPanel::saveCurrentToPreset()
{
	Config::ModelPreset preset;

	// Determine preset name
	if (m_editingPresetName.empty()) {
		// Generate default name
		std::string base =
			m_selectedModelName.empty() ? "preset" : m_selectedModelName;
		preset.name = base + "-1";
		int n = 1;
		// Ensure unique
		while (true) {
			bool found = false;
			for (const auto &p : m_presetsForModel) {
				if (p.name == preset.name) {
					found = true;
					break;
				}
			}
			if (!found)
				break;
			preset.name = base + "-" + std::to_string(++n);
		}
		m_editingPresetName = preset.name;
	} else {
		preset.name = m_editingPresetName;
	}

	// Use model path from our parallel vector
	for (size_t i = 0; i < m_modelNames.size(); ++i) {
		if (m_modelNames[i] == m_selectedModelName) {
			preset.model = m_modelPaths[i];
			break;
		}
	}
	if (preset.model.empty())
		preset.model = m_modelPath; // fallback to current model path

	// Load settings from current member state
	preset.load.modelPath = preset.model;
	if (m_ngpuLayers == "all" || m_ngpuLayers == "auto") {
		preset.load.ngpuLayers = -1;
	} else if (!m_ngpuLayers.empty()) {
		try {
			preset.load.ngpuLayers = std::stoi(m_ngpuLayers);
		} catch (...) {
			preset.load.ngpuLayers = -1;
		}
	} else {
		preset.load.ngpuLayers = -1;
	}
	try {
		preset.load.ctxSize = m_ctxSize.empty() ? 0 : std::stoi(m_ctxSize);
	} catch (...) {
	}
	preset.load.batchSize = m_batchSize;
	preset.load.ubatchSize = m_ubatchSize;
	preset.load.parallel = m_parallel;
	preset.load.flashAttn =
		m_flashAttnOptions[static_cast<size_t>(m_flashAttnIdx)];
	preset.load.kvOffload = m_kvOffload;
	preset.load.kvUnified = m_kvUnified;
	preset.load.mmap = m_mmap;
	preset.load.mlock = m_mlock;
	preset.load.fit = m_fit;
	preset.load.devicePriority = m_devicePriority;
	preset.load.splitMode =
		m_splitModeOptions[static_cast<size_t>(m_splitModeIdx)];
	preset.load.tensorSplit = tensorSplitToString();
	preset.load.cacheTypeK =
		m_cacheTypeOptions[static_cast<size_t>(m_cacheTypeKIdx)];
	preset.load.cacheTypeV =
		m_cacheTypeOptions[static_cast<size_t>(m_cacheTypeVIdx)];
	preset.load.lora = m_lora;
	preset.load.mmproj = m_mmproj;
	preset.load.modelDraft = m_modelDraft;
	try {
		preset.load.draftMax = std::stoi(m_draftMax);
	} catch (...) {
	}
	preset.load.chatTemplate = m_chatTemplate;
	preset.load.reasoningFormat =
		m_reasoningFormatOptions[static_cast<size_t>(m_reasoningFormatIdx)];
	preset.load.specType = m_specTypeOptions[static_cast<size_t>(m_specTypeIdx)];
	preset.load.cacheTypeKDraft =
		m_cacheTypeOptions[static_cast<size_t>(m_cacheTypeKDraftIdx)];
	preset.load.cacheTypeVDraft =
		m_cacheTypeOptions[static_cast<size_t>(m_cacheTypeVDraftIdx)];
	preset.load.deviceDraft = m_deviceDraft;
	preset.load.preserveThinking = m_preserveThinking;

	// Issue #103 section C. Int/double members are kept in sync with their
	// Inputs by the makeIntControls/makeDoubleControls on_change handlers.
	// Enum index 0 ("(default)") maps back to the empty string.
	preset.load.nCpuMoe = m_nCpuMoe;
	preset.load.cpuMoe = m_cpuMoe;
	preset.load.overrideTensor = m_overrideTensor;
	preset.load.ropeScaling =
		(m_ropeScalingIdx == 0)
			? std::string{}
			: m_ropeScalingOptions[static_cast<size_t>(m_ropeScalingIdx)];
	preset.load.ropeScale = m_ropeScale;
	preset.load.ropeFreqBase = m_ropeFreqBase;
	preset.load.ropeFreqScale = m_ropeFreqScale;
	preset.load.yarnOrigCtx = m_yarnOrigCtx;
	preset.load.yarnExtFactor = m_yarnExtFactor;
	preset.load.yarnAttnFactor = m_yarnAttnFactor;
	preset.load.yarnBetaSlow = m_yarnBetaSlow;
	preset.load.yarnBetaFast = m_yarnBetaFast;
	preset.load.swaFull = m_swaFull;
	preset.load.keep = m_keep;
	preset.load.numa = (m_numaIdx == 0)
						   ? std::string{}
						   : m_numaOptions[static_cast<size_t>(m_numaIdx)];
	preset.load.fitTarget =
		(m_fitTargetIdx == 0)
			? std::string{}
			: m_fitTargetOptions[static_cast<size_t>(m_fitTargetIdx)];
	preset.load.fitCtx = m_fitCtx;
	preset.load.checkTensors = m_checkTensors;
	preset.load.overrideKv = m_overrideKv;
	preset.load.loraScaled = m_loraScaled;
	preset.load.controlVector = m_controlVector;
	preset.load.controlVectorScaled = m_controlVectorScaled;
	preset.load.specDraftNMin = m_specDraftNMin;
	preset.load.specDraftPMin = m_specDraftPMin;
	preset.load.specDraftPSplit = m_specDraftPSplit;
	preset.load.cpuMoeDraft = m_cpuMoeDraft;

	// Inference settings
	preset.inference.temperature = static_cast<double>(m_temperature);
	preset.inference.topP = static_cast<double>(m_topP);
	preset.inference.topK = m_topK;
	preset.inference.minP = static_cast<double>(m_minP);
	preset.inference.repeatPenalty = static_cast<double>(m_repeatPenalty);
	preset.inference.presencePenalty = static_cast<double>(m_presencePenalty);
	preset.inference.frequencyPenalty = static_cast<double>(m_frequencyPenalty);
	try {
		preset.inference.nPredict = std::stoi(m_nPredict);
	} catch (...) {
	}
	try {
		preset.inference.seed = std::stoi(m_seed);
	} catch (...) {
	}

	// Phase 4 inference params
	preset.inference.ignoreEos = m_ignoreEos;
	preset.inference.logitBias = m_logitBias;
	preset.inference.adaptiveTarget = static_cast<double>(m_adaptiveTarget);
	preset.inference.adaptiveDecay = static_cast<double>(m_adaptiveDecay);
	preset.inference.jsonSchemaFile = m_jsonSchemaFile;
	preset.inference.grammarFile = m_grammarFile;
	preset.inference.samplerSeq = m_samplerSeq;
	preset.inference.drySequenceBreaker = m_drySequenceBreaker;
	preset.inference.backendSampling = m_backendSampling;

	if (m_modelsIni.savePreset(preset)) {
		m_presetStatus = "Saved";
		refreshPresetsForModel();
		// Select the saved preset
		for (int i = 0; i < static_cast<int>(m_presetsForModel.size()); ++i) {
			if (m_presetsForModel[i].name == preset.name) {
				m_selectedPresetIndex = i;
				break;
			}
		}

		// Restart server so new models.ini settings take effect.
		// Router mode caches models.ini at startup — a full restart is required.
		if (m_server.isRunning()) {
			spdlog::info(
				"Preset saved — restarting server to apply new settings");
			restartServer();
		}
	} else {
		m_presetStatus = "Save failed";
	}
}

void ModelsPanel::renameSelectedPreset(const std::string &newName)
{
	if (m_selectedPresetIndex < 0 ||
		m_selectedPresetIndex >= static_cast<int>(m_presetsForModel.size())) {
		return;
	}

	std::string oldName = m_presetsForModel[m_selectedPresetIndex].name;
	if (oldName == newName || newName.empty())
		return;

	// Check for duplicate name
	for (const auto &p : m_presetsForModel) {
		if (p.name == newName) {
			m_presetStatus = "Name in use";
			return;
		}
	}

	if (m_modelsIni.renamePreset(oldName, newName)) {
		m_presetStatus = "Renamed";
		refreshPresetsForModel();
		// Re-select by new name
		for (int i = 0; i < static_cast<int>(m_presetsForModel.size()); ++i) {
			if (m_presetsForModel[i].name == newName) {
				m_selectedPresetIndex = i;
				break;
			}
		}
	} else {
		m_presetStatus = "Rename failed";
	}
}

void ModelsPanel::deleteSelectedPreset()
{
	if (m_selectedPresetIndex < 0 ||
		m_selectedPresetIndex >= static_cast<int>(m_presetsForModel.size())) {
		return;
	}

	const std::string name = m_presetsForModel[m_selectedPresetIndex].name;
	// Remember the model whose preset we are deleting so we can detect whether
	// it still has any models.ini entry afterward.
	const std::string deletedModelPath = m_modelPath;

	if (!m_modelsIni.deletePreset(name)) {
		m_presetStatus = "Delete failed";
		return;
	}

	m_presetStatus = "Deleted";
	m_selectedPresetIndex = -1;
	m_editingPresetName.clear();
	refreshPresetsForModel();

	// If that was the model's last preset, the model no longer has any
	// models.ini section — it cannot be loaded, so drop it from the dropdown.
	if (m_presetsForModel.empty()) {
		refreshModelList();
		// Re-derive the active selection from the (possibly shrunk) list.
		// refreshModelList clamps m_modelDropdownIndex into range.
		if (m_modelNames.empty()) {
			m_selectedModelName.clear();
			m_modelPath.clear();
		} else {
			if (m_modelDropdownIndex < 0 ||
				m_modelDropdownIndex >= static_cast<int>(m_modelNames.size()))
				m_modelDropdownIndex = 0;
			m_selectedModelName = m_modelNames[m_modelDropdownIndex];
			m_modelPath = m_modelPaths[m_modelDropdownIndex];
		}
		// Load presets for whatever model is now selected.
		refreshPresetsForModel();
		autoSelectFirstPreset();
		updateStartStopLabel();
		spdlog::info("Deleted last preset for '{}' — removed from dropdown",
					 deletedModelPath);
	}
}

// =========================================================================
// Add-New-Model Popup Methods
// =========================================================================

void ModelsPanel::openAddModelPopup()
{
	m_addModelPath.clear();
	m_addModelError.clear();
	m_showAddModelPopup = true;
}

void ModelsPanel::cancelAddModel()
{
	m_showAddModelPopup = false;
}

void ModelsPanel::confirmAddModel()
{
	// Validate the user-supplied path (cross-platform, std::filesystem-based).
	std::string err = pathvalid::validateGgufPath(m_addModelPath);
	if (!err.empty()) {
		m_addModelError = err;
		return; // keep popup open
	}

	// Build a preset with struct-default load/inference values. The section
	// name carries a _DEFAULT suffix so it is a valid, distinct models.ini key.
	Config::ModelPreset preset;
	preset.model = pathvalid::cleanPath(m_addModelPath);
	preset.load.modelPath = preset.model;
	preset.name = pathvalid::deriveModelName(m_addModelPath) + "_DEFAULT";

	if (!m_modelsIni.savePreset(preset)) {
		m_addModelError = "Save failed";
		return; // keep popup open
	}

	// Success — close popup and surface the new model in the dropdown.
	m_showAddModelPopup = false;
	m_addModelError.clear();

	refreshModelList();
	// Select the newly added model by matching its GGUF path.
	for (size_t i = 0; i < m_modelPaths.size(); ++i) {
		if (m_modelPaths[i] == preset.model) {
			m_modelDropdownIndex = static_cast<int>(i);
			m_selectedModelName = m_modelNames[i];
			m_modelPath = m_modelPaths[i];
			break;
		}
	}
	refreshPresetsForModel();
	autoSelectFirstPreset();
	updateStartStopLabel();

	// Restart server so the new models.ini entry takes effect. Router mode
	// caches models.ini at startup, so a full restart is required. Mirror the
	// guard in saveCurrentToPreset(): force the monitor to the unloaded state
	// before relaunching, otherwise its 1Hz poll re-triggers an auto-reload of
	// the cached model right after restart (issue #71).
	if (m_server.isRunning()) {
		spdlog::info("Model added — restarting server to apply models.ini");
		m_tracker.requestUnloadAll();
		m_server.terminate();
		m_server.launch("", m_config.getServerSettings());
		m_serverRunning = false;
		m_startStopLabel = "LOAD";
	}

	spdlog::info("Added new model '{}' from '{}'", preset.name, preset.model);
}

// =========================================================================
// Server Methods
// =========================================================================

std::string ModelsPanel::selectedPresetSection() const
{
	// The preset Menu selects the actual section within the chosen GGUF; that
	// section name is the load/unload identity (#110). Two presets of the same
	// GGUF are distinct sections and load independently.
	if (m_selectedPresetIndex >= 0 &&
		m_selectedPresetIndex < static_cast<int>(m_presetsForModel.size())) {
		return m_presetsForModel[m_selectedPresetIndex].name;
	}
	// No preset highlighted: fall back to the selected model name so the button
	// still resolves to something loadable.
	if (m_modelDropdownIndex >= 0 &&
		m_modelDropdownIndex < static_cast<int>(m_modelNames.size())) {
		return m_modelNames[m_modelDropdownIndex];
	}
	return "";
}

bool ModelsPanel::isSelectedPresetLoaded() const
{
	const std::string section = selectedPresetSection();
	if (section.empty())
		return false;
	// Read the ModelStateTracker (the #110 single source of truth), NOT the raw
	// monitor poll. The tracker owns lifecycle: it is driven by this panel's
	// load/unload intent and reconciled against server truth (ingestPoll detects
	// confirmation, disappearance, and crashes). LOADED here means "we loaded it
	// and the server still has it"; a crash flips it back to LOAD via the
	// tracker.
	const auto models = m_tracker.snapshot();
	const auto it = models.find(section);
	return it != models.end() && it->second.lifecycle == ModelLifecycle::LOADED;
}

std::size_t ModelsPanel::loadedPresetCount() const
{
	// Count presets the tracker currently holds in the LOADED lifecycle (used to
	// detect "unloading the last model", #71). Tracker-owned, not the raw
	// monitor.
	const auto models = m_tracker.snapshot();
	return static_cast<std::size_t>(
		std::count_if(models.begin(), models.end(), [](const auto &kv) {
			return kv.second.lifecycle == ModelLifecycle::LOADED;
		}));
}

void ModelsPanel::onStartStopClicked()
{
	if (m_server.isRunning()) {
		// Stop: terminate process. Drop all model state + suppress polling so
		// the monitor does not query a torn-down server (issue #71).
		m_tracker.requestUnloadAll();
		m_server.terminate();
		m_serverRunning = false;
		m_startStopLabel = "LOAD"; // Ready to start again
		spdlog::info("Server stopped");
	} else {
		// Start: launch server (without model initially)
		// Launch with empty model path = server only, no model (router mode)
		bool success = m_server.launch("", // Empty model path = no model
									   m_config.getServerSettings());

		if (success) {
			// Reap any finished previous worker before reusing the slot —
			// assigning to a joinable std::thread would call std::terminate.
			joinAsyncPoll();
			m_serverStarting.store(true, std::memory_order_release);
			m_startStopLabel = "STARTING...";
			// The worker writes ONLY the m_serverStarting atomic. The render
			// loop sees the flag drop and refreshes label/state on the UI
			// thread, so no string is touched cross-thread.
			m_asyncPoll = std::thread([this] {
				for (int i = 0;
					 i < 20 && !m_asyncStop.load(std::memory_order_acquire);
					 ++i) {
					std::this_thread::sleep_for(std::chrono::milliseconds(500));
					if (m_server.isServerHealthy()) {
						break;
					}
				}
				m_serverStarting.store(false, std::memory_order_release);
				spdlog::info("Server started (no model)");
			});
		} else {
			spdlog::error("Failed to start server");
		}
	}
}

void ModelsPanel::onLoadUnloadClicked()
{
	// Resolve the preset section to act on (#110): the preset Menu selection,
	// falling back to the selected model.
	const std::string selectedPreset = selectedPresetSection();
	if (selectedPreset.empty()) {
		spdlog::warn("No preset selected, cannot load");
		return;
	}

	// Button label determines the action.
	if (m_startStopLabel == "UNLOAD") {
		const bool wasLast = loadedPresetCount() <= 1;
		// Record intent on the shared tracker so the same state machine drives
		// both UI and API unloads (#110). Last preset -> requestUnloadAll (also
		// suppresses the router auto-reload, issue #71); otherwise requestUnload
		// marks this model's coming disappearance as expected, never a crash.
		if (wasLast)
			m_tracker.requestUnloadAll();
		else
			m_tracker.requestUnload(selectedPreset);
		(void)m_server.unloadModel(selectedPreset);
		m_startStopLabel = "LOAD";
		spdlog::info("Preset unloaded: {}", selectedPreset);
	} else {
		// Guard — ignore clicks while loading is already in progress.
		if (m_modelLoading.load(std::memory_order_acquire))
			return;

		// Guard — a preset is unique server-side and cannot be loaded twice. If
		// it is already LOADED (e.g. brought in by a batch), do nothing.
		if (isSelectedPresetLoaded()) {
			spdlog::info("Preset already loaded, ignoring load: {}",
						 selectedPreset);
			return;
		}

		// Record load intent (LOADING; resumes polling) then trigger the load.
		m_tracker.requestLoad(selectedPreset);
		bool apiOk = m_server.loadModel(selectedPreset);
		if (!apiOk) {
			spdlog::error("Failed to send load request for: {}", selectedPreset);
			return; // HTTP call itself failed — stay in LOAD state.
		}

		// API call accepted: enter LOADING state and poll for actual completion.
		// The tracker (fed by the monitor poll) is the source of truth for which
		// presets are LOADED; no optimistic panel-side set to keep in sync.
		joinAsyncPoll();
		m_modelLoading.store(true, std::memory_order_release);
		m_startStopLabel = "LOADING";

		// The worker writes ONLY the m_modelLoading atomic; the render loop
		// refreshes loaded state/label from the monitor on the UI thread when it
		// sees the flag drop. Confirmation waits for THIS preset specifically.
		m_asyncPoll = std::thread([this, selectedPreset] {
			constexpr int MAX_POLLS = 40; // 40 x 500 ms = 20 s timeout
			constexpr auto POLL_INTERVAL = std::chrono::milliseconds(500);
			bool loaded = false;
			for (int i = 0;
				 i < MAX_POLLS && !m_asyncStop.load(std::memory_order_acquire);
				 ++i) {
				std::this_thread::sleep_for(POLL_INTERVAL);
				if (m_modelInfo.getStatsFor(selectedPreset).isModelLoaded) {
					loaded = true;
					break;
				} else if (!m_server.isRunning() ||
						   !m_server.isServerHealthy()) {
					// Server died during load — exit early, don't wait for
					// timeout.
					spdlog::warn("Server became unhealthy during model load");
					break;
				}
			}
			if (loaded) {
				spdlog::info("Preset loaded: {}", selectedPreset);
			} else {
				spdlog::error("Preset load timed out: {}", selectedPreset);
			}
			m_modelLoading.store(false, std::memory_order_release);
		});
	}
}

void ModelsPanel::refreshServerState()
{
	m_serverRunning = m_server.isRunning() && m_server.isServerHealthy();

	// No panel-owned loaded-set to rebuild: which presets are LOADED is read
	// live from the tracker wherever needed (isSelectedPresetLoaded / the info
	// panel via getAllStats). The tracker is the single source of truth (#110).

	// Crash recovery: the tracker decides crash-vs-clean-unload
	// deterministically and surfaces only genuine crashes via takeCrashed(). A
	// deliberate unload (UI or API) is recorded as intent, so dropping one of
	// several models is never reported here. Restart only when the tracker
	// confirms a crash and the server *process* is still up.
	//
	// Gate on isRunning() (process alive), NOT m_serverRunning (which also
	// requires a healthy /health). After a router-mode worker OOM the router
	// parent is wedged proxying to the dead child and /health can stop
	// responding (llama.cpp #18912) — that is precisely when we must restart, so
	// health must not block recovery. Skip while an async load/unload is in
	// flight to avoid racing those operations.
	const bool busy = m_modelLoading.load(std::memory_order_acquire) ||
					  m_serverStarting.load(std::memory_order_acquire);
	const auto crashed = m_tracker.takeCrashed();
	if (!crashed.empty() && m_server.isRunning() && !busy) {
		spdlog::warn("Model crashed server-side — restarting server to recover");
		restartServer();
	}
}

void ModelsPanel::restartServer()
{
	if (!m_server.isRunning())
		return;
	// Drop all model state + suppress polling BEFORE relaunching. Otherwise the
	// 1Hz poll keeps querying /models and /slots, and in router mode those
	// queries make llama-server auto-reload the cached model right after the
	// restart (issue #71). The new server launches with an empty model path, so
	// no model should come back.
	m_tracker.requestUnloadAll();
	m_server.terminate();
	m_server.launch("", m_config.getServerSettings());
	m_serverRunning = false;
	m_startStopLabel = "LOAD";
	// Do NOT probe the new server here (no /health, /models, /slots, or unload
	// calls). In router mode any query issued while no model is loaded makes
	// llama-server auto-reload the last model (#71). requestUnloadAll() above
	// already sets the tracker's skip flag so the monitor stops issuing those
	// queries, so leaving the relaunched server completely
	// unqueried is exactly what keeps it empty until the user clicks LOAD — this
	// is identical to the working preset-save restart path.
}

void ModelsPanel::updateStartStopLabel()
{
	// Guard: don't overwrite transient states during async operations
	if (m_serverStarting.load(std::memory_order_acquire)) {
		m_startStopLabel = "STARTING...";
		return;
	}
	if (m_modelLoading.load(std::memory_order_acquire)) {
		m_startStopLabel = "LOADING";
		return;
	}

	// Label derives from the SELECTED preset's lifecycle in the
	// ModelStateTracker
	// (#110), not from raw server state: UNLOAD when LOADED, LOADING while a
	// load is pending confirmation, otherwise LOAD.
	if (!m_server.isRunning()) {
		m_startStopLabel = "LOAD";
		return;
	}

	const std::string section = selectedPresetSection();
	const auto models = m_tracker.snapshot();
	const auto it = section.empty() ? models.end() : models.find(section);
	if (it != models.end() && it->second.lifecycle == ModelLifecycle::LOADED) {
		m_startStopLabel = "UNLOAD";
	} else if (it != models.end() &&
			   it->second.lifecycle == ModelLifecycle::LOADING) {
		m_startStopLabel = "LOADING";
	} else {
		m_startStopLabel = "LOAD";
	}
}
