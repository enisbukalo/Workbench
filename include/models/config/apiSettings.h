#pragma once
#include <algorithm>
#include <string>

namespace Config {

/**
 * @file apiSettings.h
 * @brief Settings for Workbench's own inbound control API.
 *
 * Distinct from ServerSettings: ServerSettings configures the *llama-server*
 * process Workbench supervises, whereas ApiSettings configures the HTTP
 * listener Workbench itself exposes so external clients can control the app
 * (restart the server, load/unload models, change config).
 *
 * The listener is started/stopped on the fly from the Settings panel; the
 * @c apiEnabled flag persists the user's choice across runs.
 */

/**
 * @brief Inbound control-API configuration.
 *
 * @note The listener binds @c apiHost / @c apiPort. "0.0.0.0" listens on all
 *       interfaces; "127.0.0.1" restricts to localhost.
 * @note When @c apiRequireKey is true, requests must present @c apiKey
 *       (Authorization: Bearer KEY); otherwise authentication is skipped.
 */
struct ApiSettings
{
	/**
	 * @brief Whether the inbound control API listener should run.
	 *
	 * Persisted so the listener can auto-start at launch. Toggled live from
	 * the Settings panel.
	 *
	 * @default false
	 */
	bool apiEnabled = false;

	/**
	 * @brief Host address the control API binds to.
	 *
	 * "0.0.0.0" listens on all interfaces; "127.0.0.1" restricts to localhost.
	 *
	 * @default "0.0.0.0"
	 */
	std::string apiHost = "0.0.0.0";

	/**
	 * @brief Port the control API listens on.
	 *
	 * Distinct from the llama-server port (ServerSettings::port, default 8080)
	 * to avoid a bind conflict when both run on the same host.
	 *
	 * @default 8090
	 * @range 1-65535
	 */
	int apiPort = 8090;

	/**
	 * @brief Whether requests must carry a valid API key.
	 *
	 * When false (default) the API is open — convenient for local/dev use but
	 * note that with @c apiHost "0.0.0.0" the control surface is reachable from
	 * the network. Enable this for any exposed deployment.
	 *
	 * @default false
	 */
	bool apiRequireKey = false;

	/**
	 * @brief Key clients must present when @c apiRequireKey is true.
	 *
	 * Sent as "Authorization: Bearer KEY". Ignored when @c apiRequireKey is
	 * false. Redacted from API config responses.
	 */
	std::string apiKey;

	/**
	 * @brief Clamp numeric fields to valid ranges in-place.
	 *
	 * Called automatically at the end of from_json.
	 */
	void validate() noexcept;
};

} // namespace Config
