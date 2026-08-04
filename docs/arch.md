# Workbench — Architecture & Design

### A TUI for managing, configuring, and controlling a local llama.cpp server and its models.

Workbench is a C++20 terminal UI built on [FTXUI](https://github.com/ArthurSonzogni/ftxui). It launches and supervises a `llama-server` process, monitors system resources (CPU / GPU / RAM), discovers and loads GGUF models, embeds a real terminal, and persists everything to JSON config — all from a single keyboard-driven TUI.

---

## Directory Structure

The tree below reflects the actual layout of the repository.

```
Workbench/
├── src/
│   ├── main.cpp                      # Entry point: logging, config load, monitor start, App::run()
│   ├── app.cpp                       # Root FTXUI screen; builds panel tree, wires dependencies
│   ├── config/                       # ConfigManager + per-section settings (JSON via nlohmann)
│   ├── core/                         # EventBus, ThemeManager
│   ├── system/                       # Monitors, llama-server process, HTTP client, PTY
│   │   ├── cpuMonitor / cpuLinux / cpuWindows
│   │   ├── ramMonitor / ramLinux / ramWindows
│   │   ├── gpuMonitor                # nvidia-smi CSV parsing
│   │   ├── systemInfo / systemInfoLinux / systemInfoWindows / systemInfoUtils
│   │   ├── systemMonitorRunner       # Background thread driving all monitors
│   │   ├── modelInfoMonitor          # Polls llama-server for loaded-model metadata
│   │   ├── httpClient                # Thin cpp-httplib wrapper
│   │   ├── llamaServerProcess(.cpp/Linux/Windows)  # Process lifecycle (pImpl)
│   │   └── pty / ptyLinux / ptyWindows             # Pseudo-terminal (forkpty / ConPTY)
│   ├── ui/panels/                    # FTXUI panels (see below)
│   └── utility/                      # modelDiscovery, modelsIni, ui_utils
│
├── include/                          # Public headers, mirrors src/ plus:
│   ├── core/I*.h                     # Interfaces for GMock-testable dependencies
│   ├── core/appDependencies.h        # DI struct passed to panels
│   ├── models/                       # Plain data structs (memoryStats, processorStats, …)
│   └── server/httplib.h              # Vendored cpp-httplib (single header)
│
├── third_party/libvterm-0.3.3/       # Vendored VT100 terminal emulator
├── tests/                            # GTest/GMock unit tests + mocks/
├── docs/arch.md                      # This document
├── CMakeLists.txt                    # Root build (C++20, FetchContent deps, platform source split)
├── Dockerfile / docker-compose.yml   # Containerized Linux + Windows-cross builds
└── build.sh                          # Build orchestration (clang-format, build, test, coverage)
```

### UI Panels (`src/ui/panels/`, `include/panels/`)

| Panel | Responsibility |
|---|---|
| `systemResourcesPanel` | Live CPU / GPU / RAM gauges and utilization tables |
| `modelInfoPanel` | Metadata of the loaded model (name, size, context, quantization) |
| `serverInfoPanel` | llama-server status / health indicator |
| `serverLogPanel` | llama-server stdout/stderr log viewer |
| `modelsPanel` | Browse / select / load models from `models.ini` |
| `terminalPanel` | Embedded shell via PTY + libvterm |
| `terminalPresetsPanel` | List / create / delete / activate terminal presets |
| `settingsPanel` | Server, UI, and terminal configuration UI |
| `statusBarPanel` | Footer: breadcrumb / key hints |

---

## Subsystems

### Configuration (`config/`)
- `ConfigManager` — Meyers singleton, **the only component that touches the config file on disk**. Loads/saves `config.json` (a single aggregate `Config::UserConfig`) via nlohmann/json with graceful degradation (missing/corrupt → defaults, then write defaults).
- Per-section settings structs: server, load, inference, UI, theme, status-bar, discovery, terminal presets.
- Reads protected by `std::shared_mutex` (`getConfig() const`); on save, publishes `config.saved` on the EventBus so interested components can react.

### Core (`core/`)
- `EventBus` — global, thread-safe publish/subscribe. String event IDs (`"config.ui.refreshRateMs"`, `"config.saved"`), wildcard `"*"`, handlers copied out under lock then invoked lock-free.
- `ThemeManager` — loads JSON themes, exposes a `shared_ptr<const ResolvedTheme>` for lock-free reads + atomic swap on theme change.
- `AppDependencies` — dependency-injection struct of interface references handed to panels (decouples panels from singletons → unit-testable with mocks).
- `I*.h` interfaces (`IConfigManager`, `ICpuMonitor`, `IMemoryMonitor`, `IGpuMonitor`, `ILlamaServerProcess`, `IModelInfoMonitor`, `IModelsIni`) — panels depend on these, not concrete singletons.

### System Monitoring (`system/`)
- `CpuMonitor`, `MemoryMonitor`, `GpuMonitor`, `SystemInfo` — singletons exposing thread-safe snapshot getters.
  - CPU/RAM: Linux parses `/proc/*`; Windows uses Win32 APIs. Split into `*Linux.cpp` / `*Windows.cpp`.
  - GPU: parses `nvidia-smi` CSV output (NVIDIA only; no-op elsewhere).
  - `SystemInfo`: one-time static hardware identity (CPU/GPU make+model, RAM/VRAM capacity, OS).
- `SystemMonitorRunner` — singleton background thread. Calls `update()` on each monitor at a dynamic interval, then `ScreenInteractive::PostEvent(Event::Custom)` to trigger a UI redraw. Refresh rate is updated live by subscribing to `config.ui.refreshRateMs` on the EventBus.

### llama-server Process Management (`system/`)
- `LlamaServerProcess` — singleton, pImpl. **The only component that spawns or kills `llama-server`.** Linux uses `fork()` + `execve()` (with `PR_SET_PDEATHSIG` so the child dies with the parent); Windows uses `CreateProcess`. Builds the CLI argv from server/load/inference settings, captures stdout/stderr on a pipe-reader thread, and exposes model load/unload/health over HTTP.

### HTTP Client (`system/httpClient`, `include/server/httplib.h`)
- `HttpClient` — thin wrapper over vendored cpp-httplib. GET/POST with timeouts. Used by `LlamaServerProcess` and `ModelInfoMonitor` to talk to the running server.

### Model Discovery & Metadata (`utility/`, `system/`)
- `ModelDiscovery` — singleton, recursively scans the configured search path for `.gguf` files (tilde-expanded), caches results for 5 minutes.
- `ModelsIni` — parses the llama-server `models.ini` preset file: a `[*]` global-defaults section plus one section per model.
- `ModelInfoMonitor` — polls the server for the loaded model and exposes its metadata to panels.

### PTY / Terminal (`system/pty*`, `terminalPanel`)
- `PtyHandler` — pseudo-terminal abstraction: `forkpty()` on Linux, ConPTY on Windows. All operations serialized by `ptyMutex_`.
- `terminalPanel` drives libvterm to render ANSI output and forwards keystrokes.

---

## Threading Model

| Thread | Owner | Job |
|---|---|---|
| Main | FTXUI `ScreenInteractive::Loop` | Input + render |
| Monitor poll | `SystemMonitorRunner` | Update CPU/GPU/RAM, post redraw event |
| Model poll | `ModelInfoMonitor` | Poll loaded-model metadata |
| Pipe reader | `LlamaServerProcess::Impl` | Read server stdout/stderr → log callback |
| Terminal read | `terminalPanel` | Drain PTY output into libvterm |

Cross-thread state is guarded by mutexes (monitors, EventBus, PTY, config) or `std::atomic` (stop flags, refresh rate).

---

## llama-server API (HTTP)

Endpoints actually used by `LlamaServerProcess` / `ModelInfoMonitor`:

| Purpose | Endpoint |
|---|---|
| Health / ready check | `GET /health` |
| Loaded model info | `GET /models` |
| Slot / busy state | `GET /slots?model=<name>` |
| Load a model (by `models.ini` section) | `POST /models/load` |
| Unload current model | `POST /models/unload` |

### Reload quirk (router mode)
When `llama-server` runs in router mode, querying `/models` or `/slots` can trigger an automatic model **load**. `ModelInfoMonitor` guards against this with an internal `m_forceUnloaded` flag so a metadata poll does not silently reload a model the user just unloaded. Keep this in mind when adding new polling.

---

## Config Layout (`~/.workbench/`)

```
~/.workbench/                 # %USERPROFILE%\.workbench on Windows
├── config.json               # Single aggregate UserConfig (server, UI, theme, presets, discovery…)
├── logs/                     # Application + llama-server logs
├── models.ini                # llama-server model preset file ([*] defaults + per-model sections)
└── themes/                   # JSON theme files (see the wiki)
```

`config.json` is written pretty-printed (4-space indent). Saving emits the `config.saved` event.

---

## Design Patterns

- **Meyers singletons** — config, monitors, event bus, theme manager, process, discovery.
- **Interface segregation** — `I*.h` headers let panels accept mocks under GMock; the real classes implement the interface directly (zero indirection cost in production).
- **pImpl** — `LlamaServerProcess`, `HttpClient` hide platform/library details behind a `unique_ptr<Impl>`.
- **Observer (EventBus)** — decoupled, string-keyed pub/sub instead of passing references everywhere.
- **Dependency injection** — `AppDependencies` struct constructed in `app.cpp` and passed to panels.
- **Platform split at build time** — `CMakeLists.txt` removes all `*Linux.cpp`/`*Windows.cpp`, then re-adds only the current platform's files.

---

## Key Design Rules

- `ConfigManager` is the only thing that touches the config file on disk.
- `LlamaServerProcess` is the only thing that spawns or kills `llama-server`.
- `HttpClient` is the only thing that touches cpp-httplib.
- Monitors own their own data and hand out **snapshots** under lock — panels never reach into monitor internals.
- Panels depend on `I*` interfaces (via `AppDependencies`), not on singletons directly.
- Shared mutable state is always mutex- or atomic-guarded.
- Settings changes are staged in the UI and committed only on explicit Save; themes can be previewed live.
