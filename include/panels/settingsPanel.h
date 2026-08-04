#pragma once

#include "IConfigManager.h"
#include "appDependencies.h"
#include "eventBus.h"
#include "terminalPresetsPanel.h"

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <string>

/**
 * @file settingsPanel.h
 * @brief Stateful settings panel for server, UI, and terminal configuration.
 *
 * This is the main configuration hub for Workbench non-model settings,
 * providing a unified interface to manage server behavior, application
 * appearance, and embedded terminal defaults.
 *
 * **Server Settings** - Network and HTTP server behavior:
 *   Host/port binding, API key authentication, request timeout (1-3600s),
 *   HTTP worker threads (-1=auto or 1-64), feature flags for Web UI,
 *   embedding mode, continuous batching, prompt caching, metrics collection.
 *
 * **UI Settings** - Application appearance and behavior:
 *   Theme selection (default/dark/light/monokai), default startup tab,
 *   system panel visibility toggle, refresh rate control (50-1000ms).
 *
 * **Terminal Settings** - Embedded terminal emulator defaults:
 *   Default shell command, initial command to execute on spawn, working
 *   directory, terminal dimensions (cols: 16-300, rows: 8-100).
 *
 * **Note**: Load and Inference settings have been moved to ModelsPanel to
 * avoid duplication. SettingsPanel now focuses exclusively on server,
 * UI, and terminal configuration.
 *
 * The panel uses a two-column layout with Server/UI settings stacked
 * vertically on the left, and Terminal settings plus an embedded
 * TerminalPresetsPanel on the right. It is stateful — member variables
 * are loaded from ConfigManager in the constructor and persisted
 * immediately via onChange callbacks whenever any setting changes.
 */
class SettingsPanel
{
  public:
	explicit SettingsPanel(AppDependencies &deps);

	ftxui::Component component();

  private:
	IConfigManager &m_config;

	void loadFromConfig();
	void saveConfig();

	// --- Server state ---
	std::string m_executablePath;
	std::string m_host;
	std::string m_port;
	std::string m_apiKey;
	int m_timeout = 600;
	std::string m_timeoutStr = std::to_string(m_timeout);
	int m_threadsHttp = -1;
	std::string m_threadsHttpStr = std::to_string(m_threadsHttp);
	int m_cacheRam = 8192;
	std::string m_cacheRamStr = std::to_string(m_cacheRam);
	int m_ctxCheckpoints = 32;
	std::string m_ctxCheckpointsStr = std::to_string(m_ctxCheckpoints);
	bool m_ui = true;
	bool m_embedding = false;
	bool m_contBatching = true;
	bool m_cachePrompt = true;
	// m_metrics removed - metrics is always enabled

	// --- Phase 2: server router params (section A) ---
	int m_modelsMax = 4;
	std::string m_modelsMaxStr = std::to_string(m_modelsMax);
	bool m_modelsAutoload = false;
	int m_checkpointMinStep = 8192;
	std::string m_checkpointMinStepStr = std::to_string(m_checkpointMinStep);
	bool m_cacheIdleSlots = true;
	int m_poolingIdx = 0; // index into m_poolingOptions
	int m_embdNormalize = -1;
	std::string m_embdNormalizeStr = std::to_string(m_embdNormalize);
	int m_reasoningIdx = 2; // index into m_reasoningOptions ("auto")
	int m_reasoningBudget = -1;
	std::string m_reasoningBudgetStr = std::to_string(m_reasoningBudget);

	// Verbose API logging - off by default, shows /models, /metrics, /slots in
	// logs
	bool m_verboseApiLogs = false;

	// --- Inbound control-API state (Workbench's own API, not llama-server) ---
	bool m_apiEnabled = false;
	std::string m_apiHost;
	int m_apiPort = 8090;
	std::string m_apiPortStr = std::to_string(m_apiPort);
	bool m_apiRequireKey = false;
	std::string m_apiKeyValue;

	// --- vLLM server state ---
	std::string m_vllmHost;
	int m_vllmPort = 8000;
	std::string m_vllmPortStr = std::to_string(m_vllmPort);

	// --- UI state ---
	int m_themeIdx = 0;		 // dropdown index
	int m_defaultTabIdx = 0; // dropdown index
	bool m_showSystemPanel = true;
	bool m_systemResourcesOnly = false;
	int m_refreshRateMs = 250;
	std::string m_refreshRateMsStr = std::to_string(m_refreshRateMs);
	int m_logRetentionDays = 7;
	std::string m_logRetentionDaysStr = std::to_string(m_logRetentionDays);
	int m_temperatureUnitIdx = 0; // index into m_temperatureUnitOptions
	int m_cpuTempGreen = 30;
	std::string m_cpuTempGreenStr = "30";
	int m_cpuTempRed = 80;
	std::string m_cpuTempRedStr = "80";
	int m_gpuTempGreen = 40;
	std::string m_gpuTempGreenStr = "40";
	int m_gpuTempRed = 90;
	std::string m_gpuTempRedStr = "90";

	// --- Terminal state ---
	std::string m_defaultShell;
	std::string m_initialCommand;
	std::string m_workingDirectory;
	int m_defaultCols = 80;
	std::string m_defaultColsStr = std::to_string(m_defaultCols);
	int m_defaultRows = 24;
	std::string m_defaultRowsStr = std::to_string(m_defaultRows);

	// Dropdown option lists (must be stable for FTXUI references)
	std::vector<std::string>
		m_themeOptions; // Populated from ThemeManager at runtime
	std::vector<std::string> m_tabOptions = { "Settings",
											  "Server Log",
											  "Terminal" };
	// Enum option lists for section-A params (must be stable for FTXUI refs).
	// pooling: empty string ("") maps to index 0 ("(default)").
	std::vector<std::string> m_poolingOptions = { "(default)", "none", "mean",
												  "cls",	   "last", "rank" };
	std::vector<std::string> m_reasoningOptions = { "on", "off", "auto" };
	std::vector<std::string> m_temperatureUnitOptions = { "celsius",
														  "fahrenheit" };

	TerminalPresetsPanel m_terminalPresets;

	ftxui::Component m_component;
};
