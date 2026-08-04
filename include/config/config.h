#pragma once
#include "apiSettings.h"
#include "serverSettings.h"
#include "modelSettings.h"
#include "uiSettings.h"
#include "userSettings.h"
#include "vllmSettings.h"
#include <optional>
#include <vector>
#include "json.hpp"

/**
 * @file config.h
 * @brief UserConfig aggregate struct and JSON serialization declarations.
 *
 * This header serves as the central aggregation point for all configuration
 * structures in the Workbench application. It includes the domain-specific
 * config headers and declares the to_json/from_json functions required for
 * nlohmann::json serialization.
 *
 * The UserConfig struct is the root configuration object that gets serialized
 * to the config file (~/.workbench/config.json). Its structure mirrors the
 * JSON format:
 * @code
 * {
 *   "server": { ... },      // ServerSettings
 *   "ui": { ... },          // UISettings
 *   "terminal": { ... },    // TerminalSettings
 *   "discovery": { ... },   // DiscoverySettings
 *   "terminalPresets": [ .. ] // vector<TerminalPreset>
 * }
 * @endcode
 *
 * Model presets (and their per-model load/inference settings) live in
 * models.ini, not in this config file. They reach llama-server via the load
 * API.
 *
 * @see ConfigManager for loading and saving configuration
 * @see serverSettings.h for ServerSettings documentation
 * @see modelSettings.h for ModelPreset / LoadSettings / InferenceSettings docs
 * @see uiSettings.h for UISettings documentation
 * @see userSettings.h for TerminalSettings documentation
 */
namespace Config {

/**
 * @brief Main configuration container.
 *
 * Aggregates all configuration categories into a single structure.
 * This is the root object that gets serialized to/from the JSON
 * config file. Each member corresponds to a top-level key in the JSON.
 *
 * This struct uses aggregate initialization, allowing initialization
 * with brace syntax: `UserConfig config{...};`
 *
 * @note All members have default values defined in their respective
 *       struct definitions, so `UserConfig config;` creates a valid
 *       configuration with sensible defaults.
 * @note The struct is designed to be trivially copyable and movable.
 *
 * @code
 * // Create with defaults
 * UserConfig config;
 *
 * // Create with custom values
 * UserConfig config{
 *     ServerSettings{.host = "0.0.0.0", .port = 8080},
 *     UISettings{},
 *     TerminalSettings{},
 *     DiscoverySettings{},
 *     {}  // terminalPresets
 * };
 * @endcode
 */
struct UserConfig
{
	/**
	 * @brief Server and network configuration.
	 *
	 * Contains settings for the HTTP server including host, port,
	 * API keys, SSL certificates, and server behavior options.
	 * These settings map directly to llama-server CLI parameters.
	 *
	 * @see serverSettings.h for detailed field documentation
	 */
	ServerSettings server;

	/**
	 * @brief Inbound control-API configuration.
	 *
	 * Configures the HTTP listener Workbench exposes for external control
	 * (restart server, load/unload models, change config). Distinct from
	 * @c server, which configures the supervised llama-server process.
	 *
	 * @see apiSettings.h for detailed field documentation
	 */
	ApiSettings api;

	/**
	 * @brief User interface configuration.
	 *
	 * Contains settings for the terminal UI including theme,
	 * default tab, system panel visibility, and refresh rate.
	 *
	 * @see uiSettings.h for detailed field documentation
	 */
	UISettings ui;

	/**
	 * @brief Terminal emulator configuration.
	 *
	 * Contains settings for the embedded terminal including
	 * default shell, initial command, working directory, and
	 * default dimensions.
	 *
	 * @see userSettings.h for TerminalSettings documentation
	 */
	TerminalSettings terminal;

	/**
	 * @brief Model discovery configuration.
	 *
	 * Contains settings for automatic model file discovery,
	 * including directories to scan for .gguf files.
	 *
	 * @see modelSettings.h for DiscoverySettings documentation
	 */
	DiscoverySettings discovery;

	/**
	 * @brief Named terminal presets.
	 *
	 * A list of named terminal configurations that appear as
	 * dynamic top-level tabs in the application. Each preset
	 * defines a command to run and optional dimensions.
	 *
	 * @see userSettings.h for TerminalPreset documentation
	 */
	std::vector<TerminalPreset> terminalPresets;

	/**
	 * @brief vLLM server connection settings.
	 *
	 * Configures an external vLLM inference server for the Model Info panel.
	 * Empty host disables vLLM monitoring.
	 *
	 * @see vllmSettings.h for detailed field documentation
	 */
	VllmSettings vllm;

	/**
	 * @brief Validate all sub-structs (server, ui, terminal, discovery,
	 *        terminalPresets, vllm).
	 *
	 * Model presets live in models.ini and are validated there, so there are no
	 * model-preset cross-field rules here.
	 *
	 * Not noexcept — calls string-assigning validates and spdlog.
	 */
	void validateAll();
};

/**
 * @name JSON Serialization Functions
 * @{
 *
 * These functions enable nlohmann::json to automatically serialize
 * and deserialize configuration structures. They follow the naming
 * convention required by the nlohmann::json library for custom types.
 *
 * The serialization is recursive - to_json for UserConfig calls
 * to_json for each member, which in turn may call to_json for
 * nested structures.
 *
 * @note These functions are declared here but defined in config.cpp.
 * @note The from_json functions use j.value(key, default) to provide
 *       graceful fallback to default values when keys are missing.
 */

/** @brief Serialize ServerSettings to JSON. */
void to_json(nlohmann::json &j, const ServerSettings &v);
/** @brief Deserialize ServerSettings from JSON. */
void from_json(const nlohmann::json &j, ServerSettings &v);

/** @brief Serialize ApiSettings to JSON. */
void to_json(nlohmann::json &j, const ApiSettings &v);
/** @brief Deserialize ApiSettings from JSON. */
void from_json(const nlohmann::json &j, ApiSettings &v);

/** @brief Serialize LoadSettings to JSON. */
void to_json(nlohmann::json &j, const LoadSettings &v);
/** @brief Deserialize LoadSettings from JSON. */
void from_json(const nlohmann::json &j, LoadSettings &v);

/** @brief Serialize InferenceSettings to JSON. */
void to_json(nlohmann::json &j, const InferenceSettings &v);
/** @brief Deserialize InferenceSettings from JSON. */
void from_json(const nlohmann::json &j, InferenceSettings &v);

/** @brief Serialize UISettings to JSON. */
void to_json(nlohmann::json &j, const UISettings &v);
/** @brief Deserialize UISettings from JSON. */
void from_json(const nlohmann::json &j, UISettings &v);

/** @brief Serialize TerminalSettings to JSON. */
void to_json(nlohmann::json &j, const TerminalSettings &v);
/** @brief Deserialize TerminalSettings from JSON. */
void from_json(const nlohmann::json &j, TerminalSettings &v);

/** @brief Serialize ModelPreset to JSON. */
void to_json(nlohmann::json &j, const ModelPreset &v);
/** @brief Deserialize ModelPreset from JSON. */
void from_json(const nlohmann::json &j, ModelPreset &v);

/** @brief Serialize TerminalPreset to JSON. */
void to_json(nlohmann::json &j, const TerminalPreset &v);
/** @brief Deserialize TerminalPreset from JSON. */
void from_json(const nlohmann::json &j, TerminalPreset &v);

/** @brief Serialize DiscoverySettings to JSON. */
void to_json(nlohmann::json &j, const DiscoverySettings &v);
/** @brief Deserialize DiscoverySettings from JSON. */
void from_json(const nlohmann::json &j, DiscoverySettings &v);

/** @brief Serialize VllmSettings to JSON. */
void to_json(nlohmann::json &j, const VllmSettings &v);
/** @brief Deserialize VllmSettings from JSON. */
void from_json(const nlohmann::json &j, VllmSettings &v);

/** @brief Serialize UserConfig to JSON. */
void to_json(nlohmann::json &j, const UserConfig &v);
/** @brief Deserialize UserConfig from JSON. */
void from_json(const nlohmann::json &j, UserConfig &v);

/** @} */

} // namespace Config
