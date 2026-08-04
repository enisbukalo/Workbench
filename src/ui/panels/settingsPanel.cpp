#include "settingsPanel.h"

#include "ThemeManager.h"
#include "controlApiServer.h"
#include "eventBus.h"
#include "ui_utils.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <iomanip>
#include <sstream>

using namespace ftxui;

namespace {
// Per-section scroll caps: max terminal lines a section's scrollable row
// viewport occupies before a vertical scrollbar appears. LESS_THAN is
// exclusive. The window border and indent live outside the scrolled vbox and
// are not counted here. On taller terminals the rows simply fit; on short ones
// the section scrolls internally so every control stays reachable.
constexpr int SERVER_SETTINGS_MAX_LINES = 16;
constexpr int UI_SETTINGS_MAX_LINES = 13;
constexpr int TERMINAL_SETTINGS_MAX_LINES = 11;
} // namespace

// ---------------------------------------------------------------------------

SettingsPanel::SettingsPanel(AppDependencies &deps)
	: m_config(deps.config), m_terminalPresets(deps.config)
{
	m_themeOptions = ThemeManager::instance().getAvailableThemes();
	if (m_themeOptions.empty()) {
		m_themeOptions.push_back("Default");
	}
	loadFromConfig();
}

void SettingsPanel::loadFromConfig()
{
	const auto cfg = m_config.getConfigSnapshot();

	// Server
	m_executablePath = cfg.server.executablePath;
	m_host = cfg.server.host;
	m_port = std::to_string(cfg.server.port);
	m_apiKey = cfg.server.apiKey;
	m_timeout = cfg.server.timeout;
	m_timeoutStr = std::to_string(m_timeout);
	m_threadsHttp = cfg.server.threadsHttp;
	m_threadsHttpStr = std::to_string(m_threadsHttp);
	m_cacheRam = cfg.server.cacheRam;
	m_cacheRamStr = std::to_string(m_cacheRam);
	m_ctxCheckpoints = cfg.server.ctxCheckpoints;
	m_ctxCheckpointsStr = std::to_string(m_ctxCheckpoints);
	m_ui = cfg.server.ui;
	m_embedding = cfg.server.embedding;
	m_contBatching = cfg.server.contBatching;
	m_cachePrompt = cfg.server.cachePrompt;
	m_verboseApiLogs = cfg.server.verboseApiLogs;

	// Inbound control API
	m_apiEnabled = cfg.api.apiEnabled;
	m_apiHost = cfg.api.apiHost;
	m_apiPort = cfg.api.apiPort;
	m_apiPortStr = std::to_string(m_apiPort);
	m_apiRequireKey = cfg.api.apiRequireKey;
	m_apiKeyValue = cfg.api.apiKey;

	// vLLM server
	m_vllmHost = cfg.vllm.host;
	m_vllmPort = cfg.vllm.port;
	m_vllmPortStr = std::to_string(m_vllmPort);

	// Server router params (section A)
	m_modelsMax = cfg.server.modelsMax;
	m_modelsMaxStr = std::to_string(m_modelsMax);
	m_modelsAutoload = cfg.server.modelsAutoload;
	m_checkpointMinStep = cfg.server.checkpointMinStep;
	m_checkpointMinStepStr = std::to_string(m_checkpointMinStep);
	m_cacheIdleSlots = cfg.server.cacheIdleSlots;
	// pooling: "" → index 0 ("(default)"); else its position in the option
	// list.
	{
		auto it = std::find(m_poolingOptions.begin(),
							m_poolingOptions.end(),
							cfg.server.pooling);
		m_poolingIdx = (it != m_poolingOptions.end())
						   ? static_cast<int>(it - m_poolingOptions.begin())
						   : 0;
	}
	m_embdNormalize = cfg.server.embdNormalize;
	m_embdNormalizeStr = std::to_string(m_embdNormalize);
	{
		auto it = std::find(m_reasoningOptions.begin(),
							m_reasoningOptions.end(),
							cfg.server.reasoning);
		m_reasoningIdx = (it != m_reasoningOptions.end())
							 ? static_cast<int>(it - m_reasoningOptions.begin())
							 : 2; // "auto"
	}
	m_reasoningBudget = cfg.server.reasoningBudget;
	m_reasoningBudgetStr = std::to_string(m_reasoningBudget);

	// UI
	auto themeIt =
		std::find(m_themeOptions.begin(), m_themeOptions.end(), cfg.ui.theme);
	m_themeIdx = (themeIt != m_themeOptions.end())
					 ? static_cast<int>(themeIt - m_themeOptions.begin())
					 : 0;
	m_defaultTabIdx = std::clamp(cfg.ui.defaultTab, 0, 2);
	m_showSystemPanel = cfg.ui.showSystemPanel;
	m_systemResourcesOnly = cfg.ui.systemResourcesOnly;
	m_refreshRateMs = cfg.ui.refreshRateMs;
	m_refreshRateMsStr = std::to_string(m_refreshRateMs);
	m_logRetentionDays = cfg.ui.logRetentionDays;
	m_logRetentionDaysStr = std::to_string(m_logRetentionDays);
	{
		auto it = std::find(m_temperatureUnitOptions.begin(),
							m_temperatureUnitOptions.end(),
							cfg.ui.temperatureUnit);
		m_temperatureUnitIdx =
			(it != m_temperatureUnitOptions.end())
				? static_cast<int>(it - m_temperatureUnitOptions.begin())
				: 0; // "celsius"
	}

	m_cpuTempGreen = cfg.ui.cpuTemperatureGreenBottom;
	m_cpuTempGreenStr = std::to_string(m_cpuTempGreen);
	m_cpuTempRed = cfg.ui.cpuTemperatureRedTop;
	m_cpuTempRedStr = std::to_string(m_cpuTempRed);
	m_gpuTempGreen = cfg.ui.gpuTemperatureGreenBottom;
	m_gpuTempGreenStr = std::to_string(m_gpuTempGreen);
	m_gpuTempRed = cfg.ui.gpuTemperatureRedTop;
	m_gpuTempRedStr = std::to_string(m_gpuTempRed);

	// Terminal
	m_defaultShell = cfg.terminal.defaultShell;
	m_initialCommand = cfg.terminal.initialCommand;
	m_workingDirectory = cfg.terminal.workingDirectory;
	m_defaultCols = cfg.terminal.defaultCols;
	m_defaultColsStr = std::to_string(m_defaultCols);
	m_defaultRows = cfg.terminal.defaultRows;
	m_defaultRowsStr = std::to_string(m_defaultRows);
}

void SettingsPanel::saveConfig()
{
	// Work on a snapshot; the modified copy is swapped in atomically via
	// setConfig() so background readers never observe a half-written config.
	auto cfg = m_config.getConfigSnapshot();

	// ========================================================================
	// Capture old values for change detection
	// ========================================================================
	int oldRefreshRateMs = cfg.ui.refreshRateMs;
	std::string oldTheme = cfg.ui.theme;
	int oldDefaultTab = cfg.ui.defaultTab;
	bool oldShowSystemPanel = cfg.ui.showSystemPanel;
	bool oldSystemResourcesOnly = cfg.ui.systemResourcesOnly;
	std::string oldTemperatureUnit = cfg.ui.temperatureUnit;

	// Inbound control-API: capture old values to detect a change that requires
	// restarting the live listener (enable toggle, host/port/auth change).
	Config::ApiSettings oldApi = cfg.api;

	// ========================================================================
	// Update Server settings
	// ========================================================================
	cfg.server.executablePath = m_executablePath;
	cfg.server.host = m_host;
	try {
		cfg.server.port = std::stoi(m_port);
	} catch (...) {
	}
	cfg.server.apiKey = m_apiKey;
	cfg.server.timeout = m_timeout;
	cfg.server.threadsHttp = m_threadsHttp;
	cfg.server.cacheRam = m_cacheRam;
	cfg.server.ctxCheckpoints = m_ctxCheckpoints;
	cfg.server.ui = m_ui;
	cfg.server.embedding = m_embedding;
	cfg.server.contBatching = m_contBatching;
	cfg.server.cachePrompt = m_cachePrompt;
	cfg.server.verboseApiLogs = m_verboseApiLogs;

	// ========================================================================
	// Update inbound control-API settings
	// ========================================================================
	cfg.api.apiEnabled = m_apiEnabled;
	cfg.api.apiHost = m_apiHost;
	cfg.api.apiPort = m_apiPort;
	cfg.api.apiRequireKey = m_apiRequireKey;
	cfg.api.apiKey = m_apiKeyValue;

	// ========================================================================
	// Update vLLM server settings
	// ========================================================================
	cfg.vllm.host = m_vllmHost;
	try {
		cfg.vllm.port = std::stoi(m_vllmPortStr);
	} catch (...) {
	}
	cfg.vllm.validate();

	// Server router params (section A)
	cfg.server.modelsMax = m_modelsMax;
	cfg.server.modelsAutoload = m_modelsAutoload;
	cfg.server.checkpointMinStep = m_checkpointMinStep;
	cfg.server.cacheIdleSlots = m_cacheIdleSlots;
	// pooling index 0 ("(default)") maps back to the empty string.
	cfg.server.pooling =
		(m_poolingIdx == 0)
			? std::string{}
			: m_poolingOptions[static_cast<size_t>(m_poolingIdx)];
	cfg.server.embdNormalize = m_embdNormalize;
	cfg.server.reasoning =
		m_reasoningOptions[static_cast<size_t>(m_reasoningIdx)];
	cfg.server.reasoningBudget = m_reasoningBudget;

	// ========================================================================
	// Update UI settings
	// ========================================================================
	cfg.ui.theme = m_themeOptions[static_cast<size_t>(m_themeIdx)];
	cfg.ui.defaultTab = m_defaultTabIdx;
	cfg.ui.showSystemPanel = m_showSystemPanel;
	cfg.ui.systemResourcesOnly = m_systemResourcesOnly;
	cfg.ui.refreshRateMs = m_refreshRateMs;
	cfg.ui.logRetentionDays = m_logRetentionDays;
	cfg.ui.temperatureUnit =
		m_temperatureUnitOptions[static_cast<size_t>(m_temperatureUnitIdx)];
	cfg.ui.cpuTemperatureGreenBottom = m_cpuTempGreen;
	cfg.ui.cpuTemperatureRedTop = m_cpuTempRed;
	cfg.ui.gpuTemperatureGreenBottom = m_gpuTempGreen;
	cfg.ui.gpuTemperatureRedTop = m_gpuTempRed;

	// ========================================================================
	// Update Terminal settings
	// ========================================================================
	cfg.terminal.defaultShell = m_defaultShell;
	cfg.terminal.initialCommand = m_initialCommand;
	cfg.terminal.workingDirectory = m_workingDirectory;
	cfg.terminal.defaultCols = m_defaultCols;
	cfg.terminal.defaultRows = m_defaultRows;

	// Swap the modified copy in before publishing/saving so subscribers that
	// re-read the config observe the new values.
	m_config.setConfig(cfg);

	// ========================================================================
	// Publish events for changed values (dynamic updates)
	// ========================================================================
	// UI refresh rate — SystemMonitorRunner subscribes to this
	if (oldRefreshRateMs != cfg.ui.refreshRateMs) {
		EventBus::publish("config.ui.refreshRateMs", &cfg.ui.refreshRateMs);
		spdlog::info("Config changed: ui.refreshRateMs = {}",
					 cfg.ui.refreshRateMs);
	}

	// UI theme — future: ThemeManager could subscribe
	if (oldTheme != cfg.ui.theme) {
		EventBus::publish("config.ui.theme", &cfg.ui.theme);
		spdlog::info("Config changed: ui.theme = {}", cfg.ui.theme);
	}

	// Default tab — future: TabManager could subscribe
	if (oldDefaultTab != cfg.ui.defaultTab) {
		EventBus::publish("config.ui.defaultTab", &cfg.ui.defaultTab);
		spdlog::info("Config changed: ui.defaultTab = {}", cfg.ui.defaultTab);
	}

	// System panel visibility — future: UI could subscribe
	if (oldShowSystemPanel != cfg.ui.showSystemPanel) {
		EventBus::publish("config.ui.showSystemPanel", &cfg.ui.showSystemPanel);
		spdlog::info("Config changed: ui.showSystemPanel = {}",
					 cfg.ui.showSystemPanel);
	}

	// System Resources Only mode — future: Renderer could subscribe
	if (oldSystemResourcesOnly != cfg.ui.systemResourcesOnly) {
		EventBus::publish("config.ui.systemResourcesOnly",
						  &cfg.ui.systemResourcesOnly);
		spdlog::info("Config changed: ui.systemResourcesOnly = {}",
					 cfg.ui.systemResourcesOnly);
	}

	// Temperature unit — SystemResourcesPanel re-reads the config each frame,
	// so no subscriber is required; published for parity with other settings.
	if (oldTemperatureUnit != cfg.ui.temperatureUnit) {
		EventBus::publish("config.ui.temperatureUnit", &cfg.ui.temperatureUnit);
		spdlog::info("Config changed: ui.temperatureUnit = {}",
					 cfg.ui.temperatureUnit);
	}

	// ========================================================================
	// Inbound control-API: apply lifecycle changes on the fly
	// ========================================================================
	// Any change to enable/host/port/auth restarts the listener so the new
	// binding/policy takes effect immediately — no app restart required.
	const bool apiChanged = oldApi.apiEnabled != cfg.api.apiEnabled ||
							oldApi.apiHost != cfg.api.apiHost ||
							oldApi.apiPort != cfg.api.apiPort ||
							oldApi.apiRequireKey != cfg.api.apiRequireKey ||
							oldApi.apiKey != cfg.api.apiKey;
	if (apiChanged) {
		auto &api = ControlApiServer::instance();
		api.stop();
		if (cfg.api.apiEnabled) {
			if (!api.start(cfg.api))
				spdlog::warn("Inbound control API failed to start on {}:{}",
							 cfg.api.apiHost,
							 cfg.api.apiPort);
		}
		spdlog::info("Inbound control API {} ({}:{})",
					 cfg.api.apiEnabled ? "enabled" : "disabled",
					 cfg.api.apiHost,
					 cfg.api.apiPort);
	}

	// ========================================================================
	// Persist to disk
	// ========================================================================
	m_config.save();
}

Component SettingsPanel::component()
{
	if (m_component)
		return m_component;

	auto onChange = [this] { saveConfig(); };

	// -----------------------------------------------------------------------
	// Shared options — colors resolved from theme at render time
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

	// -----------------------------------------------------------------------
	// Server components
	// Creates FTXUI input components for server configuration
	// API key uses password mode for privacy
	// -----------------------------------------------------------------------
	auto exePathInput =
		Input(&m_executablePath, "path/to/llama-server", inputOpt);
	auto hostInput = Input(&m_host, "127.0.0.1", inputOpt);
	auto portInput = Input(&m_port, "8080", inputOpt);

	InputOption apiKeyOpt = inputOpt;
	apiKeyOpt.password = true;
	auto apiKeyInput = Input(&m_apiKey, "API Key", apiKeyOpt);

	auto [timeoutMinus, timeoutInput, timeoutPlus] =
		makeIntControls(m_timeout, m_timeoutStr, 1, 3600, 10);
	auto [threadsHttpMinus, threadsHttpInput, threadsHttpPlus] =
		makeIntControls(m_threadsHttp, m_threadsHttpStr, -1, 64, 1);
	auto [cacheRamMinus, cacheRamInput, cacheRamPlus] =
		makeIntControls(m_cacheRam, m_cacheRamStr, 0, 1048576, 512);
	auto [ctxCheckpointsMinus, ctxCheckpointsInput, ctxCheckpointsPlus] =
		makeIntControls(m_ctxCheckpoints, m_ctxCheckpointsStr, 0, 256, 1);

	auto uiCb = Checkbox("", &m_ui, cbOpt);
	auto embeddingCb = Checkbox("", &m_embedding, cbOpt);
	auto contBatchCb = Checkbox("", &m_contBatching, cbOpt);
	auto cachePromptCb = Checkbox("", &m_cachePrompt, cbOpt);
	auto verboseApiLogsCb = Checkbox("", &m_verboseApiLogs, cbOpt);

	// Inbound control API (Workbench's own API)
	auto apiEnabledCb = Checkbox("", &m_apiEnabled, cbOpt);
	auto apiHostInput = Input(&m_apiHost, "0.0.0.0", inputOpt);
	auto [apiPortMinus, apiPortInput, apiPortPlus] =
		makeIntControls(m_apiPort, m_apiPortStr, 1, 65535, 1);
	auto apiRequireKeyCb = Checkbox("", &m_apiRequireKey, cbOpt);
	InputOption apiKeyValueOpt = inputOpt;
	apiKeyValueOpt.password = true;
	auto apiKeyValueInput = Input(&m_apiKeyValue, "API Key", apiKeyValueOpt);

	// Server router params (section A)
	auto [modelsMaxMinus, modelsMaxInput, modelsMaxPlus] =
		makeIntControls(m_modelsMax, m_modelsMaxStr, 1, 64, 1);
	auto modelsAutoloadCb = Checkbox("", &m_modelsAutoload, cbOpt);
	auto [checkpointTokMinus, checkpointTokInput, checkpointTokPlus] =
		makeIntControls(m_checkpointMinStep,
						m_checkpointMinStepStr,
						0,
						1048576,
						512);
	auto cacheIdleSlotsCb = Checkbox("", &m_cacheIdleSlots, cbOpt);
	auto poolingToggle =
		ui_utils::makeDropdown(&m_poolingOptions, &m_poolingIdx, onChange);
	auto [embdNormMinus, embdNormInput, embdNormPlus] =
		makeIntControls(m_embdNormalize, m_embdNormalizeStr, -1, 2, 1);
	auto reasoningToggle =
		ui_utils::makeDropdown(&m_reasoningOptions, &m_reasoningIdx, onChange);
	auto [reasoningBudgetMinus, reasoningBudgetInput, reasoningBudgetPlus] =
		makeIntControls(m_reasoningBudget,
						m_reasoningBudgetStr,
						-1,
						1048576,
						64);

	// -----------------------------------------------------------------------
	// vLLM server components
	// -----------------------------------------------------------------------
	auto vllmHostInput = Input(&m_vllmHost, "127.0.0.1", inputOpt);
	auto [vllmPortMinus, vllmPortInput, vllmPortPlus] =
		makeIntControls(m_vllmPort, m_vllmPortStr, 1, 65535, 1);

	// -----------------------------------------------------------------------
	// UI components
	// Creates controls for appearance and behavior settings
	// Theme and default tab use dropdowns for selection
	// -----------------------------------------------------------------------
	auto themeToggle =
		ui_utils::makeDropdown(&m_themeOptions, &m_themeIdx, onChange);

	auto defaultTabToggle =
		ui_utils::makeDropdown(&m_tabOptions, &m_defaultTabIdx, onChange);
	auto showSysPanelCb = Checkbox("", &m_showSystemPanel, cbOpt);
	auto sysResourcesOnlyCb = Checkbox("", &m_systemResourcesOnly, cbOpt);

	auto [refreshMinus, refreshInput, refreshPlus] =
		makeIntControls(m_refreshRateMs, m_refreshRateMsStr, 50, 1000, 10);

	auto [retentionMinus, retentionInput, retentionPlus] =
		makeIntControls(m_logRetentionDays, m_logRetentionDaysStr, 0, 365, 1);

	auto tempUnitToggle = ui_utils::makeDropdown(&m_temperatureUnitOptions,
												 &m_temperatureUnitIdx,
												 onChange);
	// Temperature threshold controls — per-device green/red in °C
	auto [cpuGreenMinus, cpuGreenInput, cpuGreenPlus] =
		makeIntControls(m_cpuTempGreen, m_cpuTempGreenStr, -50, 200, 5);
	auto [cpuRedMinus, cpuRedInput, cpuRedPlus] =
		makeIntControls(m_cpuTempRed, m_cpuTempRedStr, -50, 200, 5);
	auto [gpuGreenMinus, gpuGreenInput, gpuGreenPlus] =
		makeIntControls(m_gpuTempGreen, m_gpuTempGreenStr, -50, 200, 5);
	auto [gpuRedMinus, gpuRedInput, gpuRedPlus] =
		makeIntControls(m_gpuTempRed, m_gpuTempRedStr, -50, 200, 5);

	// -----------------------------------------------------------------------
	// Terminal components
	// Creates inputs for embedded terminal emulator configuration
	// Shell, command, and working directory are free-text inputs
	// Cols/rows use integer controls with reasonable bounds
	// -----------------------------------------------------------------------
	auto shellInput = Input(&m_defaultShell, "system default", inputOpt);
	auto initCmdInput = Input(&m_initialCommand, "none", inputOpt);
	auto workDirInput = Input(&m_workingDirectory, "current", inputOpt);

	auto [colsMinus, colsInput, colsPlus] =
		makeIntControls(m_defaultCols, m_defaultColsStr, 16, 300, 1);
	auto [rowsMinus, rowsInput, rowsPlus] =
		makeIntControls(m_defaultRows, m_defaultRowsStr, 8, 100, 1);

	// -----------------------------------------------------------------------
	// Container — two-column layout (Server/UI left, Terminal right)
	// -----------------------------------------------------------------------
	auto presetsComponent = m_terminalPresets.component();
	auto container = Container::Horizontal({
		Container::Vertical({
			// Left column: Server, UI
			exePathInput,
			hostInput,
			portInput,
			apiKeyInput,
			timeoutMinus,
			timeoutInput,
			timeoutPlus,
			threadsHttpMinus,
			threadsHttpInput,
			threadsHttpPlus,
			cacheRamMinus,
			cacheRamInput,
			cacheRamPlus,
			ctxCheckpointsMinus,
			ctxCheckpointsInput,
			ctxCheckpointsPlus,
			uiCb,
			embeddingCb,
			contBatchCb,
			cachePromptCb,
			verboseApiLogsCb,
			apiEnabledCb,
			apiHostInput,
			apiPortMinus,
			apiPortInput,
			apiPortPlus,
			apiRequireKeyCb,
			apiKeyValueInput,
			vllmHostInput,
			vllmPortMinus,
			vllmPortInput,
			vllmPortPlus,
			modelsMaxMinus,
			modelsMaxInput,
			modelsMaxPlus,
			modelsAutoloadCb,
			checkpointTokMinus,
			checkpointTokInput,
			checkpointTokPlus,
			cacheIdleSlotsCb,
			poolingToggle,
			embdNormMinus,
			embdNormInput,
			embdNormPlus,
			reasoningToggle,
			reasoningBudgetMinus,
			reasoningBudgetInput,
			reasoningBudgetPlus,
			themeToggle,
			defaultTabToggle,
			showSysPanelCb,
			sysResourcesOnlyCb,
			refreshMinus,
			refreshInput,
			refreshPlus,
			retentionMinus,
			retentionInput,
			retentionPlus,
			tempUnitToggle,
			cpuGreenMinus,
			cpuGreenInput,
			cpuGreenPlus,
			cpuRedMinus,
			cpuRedInput,
			cpuRedPlus,
			gpuGreenMinus,
			gpuGreenInput,
			gpuGreenPlus,
			gpuRedMinus,
			gpuRedInput,
			gpuRedPlus,
		}),
		Container::Vertical({
			// Right column: Terminal + Presets
			shellInput,
			initCmdInput,
			workDirInput,
			colsMinus,
			colsInput,
			colsPlus,
			rowsMinus,
			rowsInput,
			rowsPlus,
			presetsComponent,
		}),
	});

	m_component = Renderer(container, [=, this] {
		// === Left column: Server, UI, Terminal ===
		Elements leftElements;

		// Server Settings
		{
			Elements rows;
			rows.push_back(ui_utils::settingRowComponent("Executable Path",
														 exePathInput->Render(),
														 &m_executablePath));
			rows.push_back(ui_utils::settingRowComponent("Host",
														 hostInput->Render(),
														 &m_host));
			rows.push_back(ui_utils::settingRowComponent("Port",
														 portInput->Render(),
														 &m_port));
			rows.push_back(ui_utils::settingRowComponent("API Key",
														 apiKeyInput->Render(),
														 &m_apiKey));
			rows.push_back(ui_utils::numberRow("Timeout",
											   timeoutMinus->Render(),
											   timeoutInput->Render(),
											   timeoutPlus->Render(),
											   m_timeoutStr));
			rows.push_back(ui_utils::numberRow("HTTP Threads",
											   threadsHttpMinus->Render(),
											   threadsHttpInput->Render(),
											   threadsHttpPlus->Render(),
											   m_threadsHttpStr));
			rows.push_back(ui_utils::numberRow("Cache RAM (MiB)",
											   cacheRamMinus->Render(),
											   cacheRamInput->Render(),
											   cacheRamPlus->Render(),
											   m_cacheRamStr));
			rows.push_back(ui_utils::numberRow("Ctx Checkpoints",
											   ctxCheckpointsMinus->Render(),
											   ctxCheckpointsInput->Render(),
											   ctxCheckpointsPlus->Render(),
											   m_ctxCheckpointsStr));
			rows.push_back(ui_utils::checkboxRow("UI", uiCb->Render()));
			rows.push_back(
				ui_utils::checkboxRow("Embedding Mode", embeddingCb->Render()));
			rows.push_back(ui_utils::checkboxRow("Continuous Batching",
												 contBatchCb->Render()));
			rows.push_back(
				ui_utils::checkboxRow("Cache Prompt", cachePromptCb->Render()));
			rows.push_back(ui_utils::checkboxRow("Verbose API Logs",
												 verboseApiLogsCb->Render()));
			rows.push_back(ui_utils::numberRow("Models Max",
											   modelsMaxMinus->Render(),
											   modelsMaxInput->Render(),
											   modelsMaxPlus->Render(),
											   m_modelsMaxStr));
			rows.push_back(ui_utils::checkboxRow("Models Autoload",
												 modelsAutoloadCb->Render()));
			rows.push_back(ui_utils::numberRow("Checkpoint Min Step (tokens)",
											   checkpointTokMinus->Render(),
											   checkpointTokInput->Render(),
											   checkpointTokPlus->Render(),
											   m_checkpointMinStepStr));
			rows.push_back(ui_utils::checkboxRow("Cache Idle Slots",
												 cacheIdleSlotsCb->Render()));
			rows.push_back(
				ui_utils::checkboxRow("Pooling", poolingToggle->Render()));
			rows.push_back(ui_utils::numberRow("Embd Normalize",
											   embdNormMinus->Render(),
											   embdNormInput->Render(),
											   embdNormPlus->Render(),
											   m_embdNormalizeStr));
			rows.push_back(
				ui_utils::checkboxRow("Reasoning", reasoningToggle->Render()));
			rows.push_back(ui_utils::numberRow("Reasoning Budget",
											   reasoningBudgetMinus->Render(),
											   reasoningBudgetInput->Render(),
											   reasoningBudgetPlus->Render(),
											   m_reasoningBudgetStr));
			leftElements.push_back(window(
				text("Server Settings") | bold |
					color(ThemeManager::instance().getActive()->title),
				hbox({ text("    "),
					   vbox(std::move(rows)) | xflex | vscroll_indicator |
						   yframe |
						   size(HEIGHT, LESS_THAN, SERVER_SETTINGS_MAX_LINES) }),
				ftxui::EMPTY));
		}

		// Inbound Control API Settings (Workbench's own API)
		{
			Elements rows;
			rows.push_back(
				ui_utils::checkboxRow("Enabled", apiEnabledCb->Render()));
			rows.push_back(ui_utils::settingRowComponent("Host",
														 apiHostInput->Render(),
														 &m_apiHost));
			rows.push_back(ui_utils::numberRow("Port",
											   apiPortMinus->Render(),
											   apiPortInput->Render(),
											   apiPortPlus->Render(),
											   m_apiPortStr));
			rows.push_back(ui_utils::checkboxRow("Require API Key",
												 apiRequireKeyCb->Render()));
			rows.push_back(
				ui_utils::settingRowComponent("API Key",
											  apiKeyValueInput->Render(),
											  &m_apiKeyValue));
			leftElements.push_back(window(
				text("Control API") | bold |
					color(ThemeManager::instance().getActive()->title),
				hbox({ text("    "),
					   vbox(std::move(rows)) | xflex | vscroll_indicator |
						   yframe |
						   size(HEIGHT, LESS_THAN, UI_SETTINGS_MAX_LINES) }),
				ftxui::EMPTY));
		}

		// vLLM Server Settings
		{
			Elements rows;
			rows.push_back(ui_utils::settingRowComponent("Host",
														 vllmHostInput->Render(),
														 &m_vllmHost));
			rows.push_back(ui_utils::numberRow("Port",
											   vllmPortMinus->Render(),
											   vllmPortInput->Render(),
											   vllmPortPlus->Render(),
											   m_vllmPortStr));
			leftElements.push_back(window(
				text("VLLM Server") | bold |
					color(ThemeManager::instance().getActive()->title),
				hbox({ text("    "),
					   vbox(std::move(rows)) | xflex | vscroll_indicator |
						   yframe |
						   size(HEIGHT, LESS_THAN, UI_SETTINGS_MAX_LINES) }),
				ftxui::EMPTY));
		}

		// UI Settings
		{
			Elements rows;
			rows.push_back(
				ui_utils::checkboxRow("Theme", themeToggle->Render()));
			rows.push_back(ui_utils::checkboxRow("Default Tab",
												 defaultTabToggle->Render()));
			rows.push_back(ui_utils::checkboxRow("Show System Panel",
												 showSysPanelCb->Render()));
			rows.push_back(ui_utils::checkboxRow("System Resources Only",
												 sysResourcesOnlyCb->Render()));
			rows.push_back(ui_utils::numberRow("System Info Refresh Rate",
											   refreshMinus->Render(),
											   refreshInput->Render(),
											   refreshPlus->Render(),
											   m_refreshRateMsStr));
			rows.push_back(ui_utils::numberRow("Log Retention (days)",
											   retentionMinus->Render(),
											   retentionInput->Render(),
											   retentionPlus->Render(),
											   m_logRetentionDaysStr));
			rows.push_back(
				ui_utils::checkboxRow("Temp Unit", tempUnitToggle->Render()));
			rows.push_back(hbox({
				text("CPU Temp Range (°C)") | bold |
					color(ThemeManager::instance().getActive()->title) | xflex,
				cpuGreenMinus->Render(),
				cpuGreenInput->Render(),
				cpuGreenPlus->Render(),
				text(" / "),
				cpuRedMinus->Render(),
				cpuRedInput->Render(),
				cpuRedPlus->Render(),
			}));
			rows.push_back(hbox({
				text("GPU Temp Range (°C)") | bold |
					color(ThemeManager::instance().getActive()->title) | xflex,
				gpuGreenMinus->Render(),
				gpuGreenInput->Render(),
				gpuGreenPlus->Render(),
				text(" / "),
				gpuRedMinus->Render(),
				gpuRedInput->Render(),
				gpuRedPlus->Render(),
			}));
			leftElements.push_back(window(
				text("UI Settings") | bold |
					color(ThemeManager::instance().getActive()->title),
				hbox({ text("    "),
					   vbox(std::move(rows)) | xflex | vscroll_indicator |
						   yframe |
						   size(HEIGHT, LESS_THAN, UI_SETTINGS_MAX_LINES) }),
				ftxui::EMPTY));
		}

		// === Right column: Terminal ===
		Elements rightElements;
		{
			Elements rows;
			rows.push_back(ui_utils::settingRowComponent("Default Shell",
														 shellInput->Render(),
														 &m_defaultShell));
			rows.push_back(ui_utils::settingRowComponent("Initial Command",
														 initCmdInput->Render(),
														 &m_initialCommand));
			rows.push_back(ui_utils::settingRowComponent("Working Directory",
														 workDirInput->Render(),
														 &m_workingDirectory));
			rows.push_back(ui_utils::numberRow("Default Cols",
											   colsMinus->Render(),
											   colsInput->Render(),
											   colsPlus->Render(),
											   m_defaultColsStr));
			rows.push_back(ui_utils::numberRow("Default Rows",
											   rowsMinus->Render(),
											   rowsInput->Render(),
											   rowsPlus->Render(),
											   m_defaultRowsStr));
			rightElements.push_back(
				window(text("Terminal Settings") | bold |
						   color(ThemeManager::instance().getActive()->title),
					   hbox({ text("    "),
							  vbox(std::move(rows)) | xflex | vscroll_indicator |
								  yframe |
								  size(HEIGHT,
									   LESS_THAN,
									   TERMINAL_SETTINGS_MAX_LINES) }),
					   ftxui::EMPTY));
		}
		rightElements.push_back(presetsComponent->Render());

		auto leftCol = vbox(std::move(leftElements)) | flex;
		auto rightCol = vbox(std::move(rightElements)) | flex;

		return hbox({ leftCol, separatorLight(), rightCol });
	});

	// Translate mouse wheel into focus movement so the yframe-scrolled sections
	// respond to the wheel (yframe is render-only and tracks the focused
	// control, so moving focus with the wheel scrolls the focused section).
	// Also save on Return key so the user can explicitly persist settings.
	m_component = m_component | CatchEvent([this, container](Event event) {
					  if (event == Event::Return) {
						  saveConfig();
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
