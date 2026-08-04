#pragma once

#include <algorithm>
#include <string>

namespace Config {

/**
 * @file vllmSettings.h
 * @brief Settings for an external vLLM server.
 *
 * Distinct from ServerSettings: ServerSettings configures the supervised
 * llama-server process. VllmSettings configures an independent vLLM
 * inference server that the Model Info panel monitors for metrics.
 *
 * An empty @c host disables vLLM monitoring entirely.
 */

/**
 * @brief vLLM server connection settings.
 *
 * @note Empty host means vLLM monitoring is disabled.
 * @note Default port is 8000 (vLLM default).
 */
struct VllmSettings
{
	/**
	 * @brief Host address of the vLLM server.
	 *
	 * Empty string disables vLLM monitoring.
	 *
	 * @default ""
	 */
	std::string host;

	/**
	 * @brief Port the vLLM server listens on.
	 *
	 * @default 8000
	 * @range 1-65535
	 */
	int port = 8000;

	/**
	 * @brief Clamp numeric fields to valid ranges in-place.
	 *
	 * Called automatically at the end of from_json.
	 */
	void validate() noexcept;
};

} // namespace Config