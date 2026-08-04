/**
 * @file app.cpp
 * @brief Main application implementation.
 *
 * Implements the App class that orchestrates the Workbench TUI layout
 * and initializes the background monitoring system. Creates a grid
 * layout with system resources, models, and settings panels.
 */

#include "app.h"
#include "ThemeManager.h"
#include "appDependencies.h"
#include "batchStateTracker.h"
#include "batchStore.h"
#include "batchesPanel.h"
#include "configManager.h"
#include "cpuMonitor.h"
#include "gpuMonitor.h"
#include "llamaServerProcess.h"
#include "modelInfoMonitor.h"
#include "modelStateTracker.h"
#include "modelsIni.h"
#include "modelsPanel.h"
#include "ramMonitor.h"
#include "serverLogPanel.h"
#include "settingsPanel.h"
#include "statusBarPanel.h"
#include "statusBarSettings.h"
#include "systemMonitorRunner.h"
#include "systemResourcesPanel.h"
#include "terminalPanel.h"
#include "terminalPresetsPanel.h"
#include "vllmMonitor.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <spdlog/spdlog.h>

#include <memory>
#include <string>
#include <vector>

using namespace ftxui;

void ::App::run()
{
	spdlog::info("App::run() - initializing");

	// Note: the llama-server log is no longer truncated here. LlamaServerProcess
	// auto-starts the server in main() before App::run(), and launch() backs up
	// then lets llama-server truncate its own --log-file. Truncating again here
	// would wipe the freshly-started server's startup log.

	auto screen = ftxui::ScreenInteractive::Fullscreen();

	// Disable FTXUI's forced Ctrl-C handling. By default FTXUI generates
	// SIGABRT for Ctrl-C if unhandled. We intercept it ourselves and forward
	// the ETX byte (\x03) to the PTY so the shell process gets SIGINT.
	screen.ForceHandleCtrlC(false);

	// Load config and start the system monitor singleton with the screen
	// reference This allows it to trigger redraws when monitor data updates
	const auto config = ConfigManager::instance().getConfigSnapshot();
	SystemMonitorRunner::instance().start(&screen, config.ui.refreshRateMs);

	// Initialize the theme system BEFORE constructing panels — SettingsPanel
	// reads getAvailableThemes() in its constructor to build the theme dropdown.
	// Themes live next to the config; default.json is seeded/refreshed here.
	{
		std::string userThemes = ConfigManager::getConfigDir() + "/themes";
		ThemeManager::instance().initialize(userThemes);
	}

	// Build dependency injection struct from singleton instances
	AppDependencies deps{
		ConfigManager::instance(),		// IConfigManager&
		LlamaServerProcess::instance(), // ILlamaServerProcess&
		ModelInfoMonitor::instance(),	// IModelInfoMonitor&
		ModelStateTracker::instance(),	// IModelStateTracker&
		ModelsIni::instance(),			// IModelsIni&
		BatchStore::instance(),			// IBatchStore&
		BatchStateTracker::instance(),	// IBatchStateTracker&
		CpuMonitor::instance(),			// ICpuMonitor&
		MemoryMonitor::instance(),		// IMemoryMonitor&
		GpuMonitor::instance()			// IGpuMonitor&
	};

	TerminalPanel terminalPanel(screen);
	SettingsPanel settingsPanel(deps);
	ModelsPanel modelsPanel(deps);
	BatchesPanel batchesPanel(deps);
	ServerLogPanel serverLogPanel(screen);

	// Load status-bar settings once (no hot reload) and build the bottom bar.
	StatusBarPanel statusBar(
		Config::loadStatusBarSettings(Config::statusBarSettingsPath()));

	std::vector<std::string> tabValues{ "App Settings",
										"Model Settings",
										"Batches",
										"Server Log",
										"Terminal" };
	// Index of the built-in Terminal tab in tabValues above. The terminal
	// event/capture paths below key on this — keep it in sync if tabs are
	// reordered. (Batches was inserted at index 2, shifting Terminal to 4.)
	constexpr int kTerminalTabIndex = 4;
	int selectedTab = 0;
	auto tabToggle = Toggle(&tabValues, &selectedTab);

	// Settings tab - interactive configuration components + terminal presets
	auto settingsInner = Container::Vertical({
		settingsPanel.component(),
	});
	auto settingsContent = Renderer(settingsInner, [&] {
		return window(text(""), flex(settingsInner->Render()), ftxui::EMPTY) |
			   flex;
	});

	// Model tab - interactive configuration components
	auto modelInner = Container::Vertical({
		modelsPanel.component(),
	});
	auto modelContent = Renderer(modelInner, [&] {
		return window(text(""), flex(modelInner->Render()), ftxui::EMPTY) | flex;
	});

	// Batches tab - manage named preset batches (sits between Model Settings and
	// Server Log).
	auto batchesInner = Container::Vertical({
		batchesPanel.component(),
	});
	auto batchesContent = Renderer(batchesInner, [&] {
		return window(text(""), flex(batchesInner->Render()), ftxui::EMPTY) |
			   flex;
	});

	auto terminalContent = terminalPanel.component();

	// Server Log tab - shows live output from llama-server
	auto logOutputContent = serverLogPanel.component();

	// Spawn ServerLogPanel terminal (watching the log file)
	serverLogPanel.start();

	auto tabContainer = Container::Tab({ settingsContent,
										 modelContent,
										 batchesContent,
										 logOutputContent,
										 terminalContent },
									   &selectedTab);

	// Dynamically added tabs - loaded from config.
	// Store dynamic terminal panels so they survive until the event loop ends.
	struct DynamicTerminal
	{
		std::unique_ptr<TerminalPanel> panel;
		int tabIndex;
		std::string presetName; // Track which preset this is
	};
	std::vector<DynamicTerminal> dynamicTerminals;

	// Load terminals from config
	// (config already loaded above for SystemMonitorRunner)
	for (const auto &preset : config.terminalPresets) {
		auto panel =
			std::make_unique<TerminalPanel>(screen, preset.initialCommand);
		auto component = panel->component();
		tabValues.push_back(preset.name);
		tabContainer->Add(component);
		int idx = static_cast<int>(tabValues.size()) - 1;
		dynamicTerminals.push_back({ std::move(panel), idx, preset.name });
	}

	spdlog::info("Spawned {} terminal preset(s)", dynamicTerminals.size());

	auto interactive = Container::Vertical({ tabToggle, tabContainer }) | flex;

	// Spawn all terminals eagerly so they're ready when the user switches tabs.
	terminalPanel.spawn();
	for (auto &dt : dynamicTerminals) {
		dt.panel->spawn();
	}

	int prevTab = selectedTab;

	auto container = Renderer(interactive, [&] {
		// Auto-capture when switching to a terminal tab.
		if (selectedTab != prevTab) {
			prevTab = selectedTab;
			// Batches tab (index 2): refresh its preset list from models.ini,
			// which the Model Settings panel may have mutated while hidden.
			if (selectedTab == 2) {
				batchesPanel.onShown();
			}
			if (selectedTab == kTerminalTabIndex) {
				terminalPanel.setCapturing(true);
			}
			for (auto &dt : dynamicTerminals) {
				if (selectedTab == dt.tabIndex) {
					dt.panel->setCapturing(true);
				}
			}
		}

		// Check if any active terminal tab is capturing input.
		bool anyCapturing = false;
		if (selectedTab == kTerminalTabIndex && terminalPanel.isCapturing()) {
			anyCapturing = true;
		}
		for (auto &dt : dynamicTerminals) {
			if (selectedTab == dt.tabIndex && dt.panel->isCapturing()) {
				anyCapturing = true;
			}
		}

		auto panel = interactive->Render() | borderRounded;
		if (anyCapturing) {
			auto theme = ThemeManager::instance().getActive();
			panel = panel | color(theme->capturingBorder);
		}

		// Check for System Resources Only mode.
		auto cfg = ConfigManager::instance().getConfigSnapshot();
		bool sysResOnly = cfg.ui.systemResourcesOnly;

		if (sysResOnly) {
			// System Resources Only mode — the panel keeps its natural
			// (compact) height and the filler below absorbs the leftover
			// vertical space. Do not give the panel `flex` here: that stretches
			// the row, and the Load gauges' internal `yflex` (which exists to
			// align them to the tallest column in normal mode) then grows to
			// fill it.
			return vbox({
					   SystemResourcesPanel::render(
						   deps.cpu,
						   deps.mem,
						   deps.gpu,
						   deps.modelInfo,
						   std::ref(VllmMonitor::instance())),
					   filler(),
					   hbox({ filler(), text("Ctrl+U to exit"), filler() }) |
						   dim,
				   }) |
				   flex;
		}

		// Normal mode — full layout.
		return vbox({ SystemResourcesPanel::render(
						  deps.cpu,
						  deps.mem,
						  deps.gpu,
						  deps.modelInfo,
						  std::ref(VllmMonitor::instance())),
					  separatorCharacter("*") | bold |
						  color(ThemeManager::instance().getActive()->separator),
					  panel,
					  statusBar.render(deps.server, tabValues[selectedTab]) }) |
			   flex;
	});

	// When a terminal tab is active, intercept keyboard events before
	// the Toggle component consumes them (e.g. arrow keys, Tab, chars).
	auto root =
		container | CatchEvent([&](Event event) {
			// Ctrl+U toggles "System Resources Only" mode.
			// When a terminal is capturing, forward to PTY (do not intercept).
			const std::string &raw = event.input();
			if (raw.size() == 1 && raw[0] == '\x15') {
				// If a terminal tab is capturing, let the PTY handle it.
				if (selectedTab == kTerminalTabIndex &&
					terminalPanel.isCapturing()) {
					terminalPanel.handleEvent(event);
					return true;
				}
				for (auto &dt : dynamicTerminals) {
					if (selectedTab == dt.tabIndex && dt.panel->isCapturing()) {
						dt.panel->handleEvent(event);
						return true;
					}
				}
				// Not in a capturing terminal — toggle the mode.
				auto cfg = ConfigManager::instance().getConfigSnapshot();
				cfg.ui.systemResourcesOnly = !cfg.ui.systemResourcesOnly;
				ConfigManager::instance().setConfig(cfg);
				ConfigManager::instance().save();
				spdlog::info("System Resources Only mode {}",
							 cfg.ui.systemResourcesOnly ? "enabled"
														: "disabled");
				screen.PostEvent(Event::Custom); // trigger redraw
				return true;
			}

			// Ctrl+C — forward ETX byte to the active terminal's PTY so the
			// shell's line discipline generates SIGINT for the foreground
			// process group. This prevents FTXUI from quitting the app.
			if (event == Event::CtrlC) {
				if (selectedTab == kTerminalTabIndex &&
					terminalPanel.isCapturing()) {
					terminalPanel.sendCtrlC();
					return true;
				}
				for (auto &dt : dynamicTerminals) {
					if (selectedTab == dt.tabIndex && dt.panel->isCapturing()) {
						dt.panel->sendCtrlC();
						return true;
					}
				}
				screen.Exit();
				return true;
			}

			if (selectedTab == kTerminalTabIndex &&
				terminalPanel.wantsEvent(event)) {
				return terminalPanel.handleEvent(event);
			}
			for (auto &dt : dynamicTerminals) {
				if (selectedTab == dt.tabIndex && dt.panel->wantsEvent(event)) {
					return dt.panel->handleEvent(event);
				}
			}
			return false;
		});

	screen.Loop(root);

	spdlog::info("App::run() - exiting");
}
