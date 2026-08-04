#pragma once

#include "IBatchStore.h"
#include "IConfigManager.h"
#include "ILlamaServerProcess.h"
#include "IModelStateTracker.h"
#include "IModelsIni.h"
#include "apiSettings.h"

/**
 * @file apiRoutes.h
 * @brief Route-registration seam and auth guard for the inbound control API.
 *
 * This module is the single place all control-API route handlers live. It is
 * deliberately decoupled from singleton access so the handlers can be driven
 * from unit tests via mock dependencies injected through @c ControlApiDeps.
 *
 * Lifecycle: @c registerControlApiRoutes is called once per @c httplib::Server
 * lifetime, inside @c ControlApiServer::start() after bind_to_port succeeds and
 * before the accept thread is spawned. The @c ControlApiDeps struct is consumed
 * by value (its interface references dangle after the call but that is fine —
 * httplib captures the handler lambda, which holds a copy of the struct).
 *
 * @note This module owns no threads or resources; all lifetime management stays
 *       in @c ControlApiServer.
 *
 * @see controlApiServer.h
 * @see ILlamaServerProcess.h, IModelStateTracker.h, IConfigManager.h
 */

// Forward-declare httplib types to keep this header lightweight.  The .cpp
// includes httplib.h for the full definitions needed by handler bodies.
namespace httplib {
struct Request;
struct Response;
class Server;
} // namespace httplib

/**
 * @struct ControlApiDeps
 * @brief Dependency bundle injected into every control-API route handler.
 *
 * All interface members are non-owning references whose lifetimes must exceed
 * the @c httplib::Server they are registered with. In production the singletons
 * (@c LlamaServerProcess::instance(), @c ModelStateTracker::instance(),
 * @c ConfigManager::instance()) satisfy this automatically. In tests, mock
 * objects stored in the fixture satisfy it.
 *
 * @c apiSettings is copied by value: callers may destroy their @c ApiSettings
 * after calling @c registerControlApiRoutes.
 */
struct ControlApiDeps
{
	/**
	 * @brief The supervised llama-server process.
	 *
	 * Used by restart handlers to @c terminate() and @c launch() the process.
	 */
	ILlamaServerProcess &server;

	/**
	 * @brief Application configuration source.
	 *
	 * Provides @c getServerSettings() so restart handlers can re-launch with
	 * the current (possibly user-modified) server settings.
	 */
	IConfigManager &config;

	/**
	 * @brief Models.ini accessor used by /models list and load-validation.
	 *
	 * Provides @c getModelNames() to enumerate available presets and
	 * @c getPreset() to read their load/inference parameters (issue #107).
	 */
	IModelsIni &models;

	/**
	 * @brief Named-batch store backing the /batches endpoints (#111).
	 *
	 * Provides @c getBatchNames()/@c getBatch() for listing and resolving a
	 * batch's preset section names, which the load/unload handlers iterate.
	 */
	IBatchStore &batches;

	/**
	 * @brief Single source of truth for model load/unload lifecycle (#110).
	 *
	 * Load/unload handlers record intent here (requestLoad/requestUnload/
	 * requestUnloadAll) so the UI and the API drive the exact same state machine
	 * — a single-model unload is never mistaken for a crash by the UI.
	 */
	IModelStateTracker &tracker;

	/**
	 * @brief Auth policy snapshot for this listener lifetime.
	 *
	 * Copied from the @c ApiSettings passed to @c ControlApiServer::start() so
	 * the key check remains stable even if the user updates settings mid-run.
	 */
	Config::ApiSettings apiSettings;
};

/**
 * @brief Decide whether a request carries a valid API key.
 *
 * When @c settings.apiRequireKey is @c false every request is authorised
 * (open mode). When @c true the request must supply:
 * @code
 *   Authorization: Bearer <key>
 * @endcode
 * where @c <key> matches @c settings.apiKey exactly (case-sensitive string
 * comparison).
 *
 * @param req      The incoming HTTP request.
 * @param settings Auth policy to evaluate against.
 * @return @c true if the request is authorised; @c false otherwise.
 *
 * @note The comparison uses @c std::string::operator== which is NOT
 *       timing-safe. A constant-time memcmp would be needed to resist
 *       timing-oracle attacks from a remote adversary. For a local control
 *       surface behind a firewall this is considered acceptable.
 */
[[nodiscard]] bool isAuthorized(const httplib::Request &req,
								const Config::ApiSettings &settings);

/**
 * @brief Register all control-API routes on @p svr.
 *
 * Routes registered:
 *  - @c POST /server/restart — terminate llama-server then relaunch it empty,
 *    mirroring the UI restart path in @c ModelsPanel::restartServer() (issue
 * #106).
 *  - @c GET  /models — list all model presets from models.ini (issue #107).
 *  - @c POST /models/load — load a named preset; mirrors UI LOAD ordering (issue
 * #107).
 *  - @c POST /models/unload — body-driven unload; mirrors UI UNLOAD ordering
 *    (issue #107). No body or no @c "model" key → unload ALL currently-loaded
 *    models (@c scope=="all"). Body @c {"model":"<name>"} → validate against
 *    models.ini and unload that specific model (@c scope=="model"). In both
 *    modes the tracker records the unload intent first (requestUnloadAll /
 *    requestUnload) to suppress router auto-reload (issue #71).
 *  - @c GET  /config/app — READ-ONLY snapshot of the app/server/api/ui/terminal/
 *    discovery settings (issue #108). Secrets (@c server.apiKey,
 *    @c server.apiKeyFile, @c server.sslKeyFile, @c api.apiKey) are redacted to
 *    a sentinel when non-empty. No write counterpart: app settings only take
 *    effect on app restart, so changing them live would mislead.
 *  - @c GET  /config/models — full @c ModelPreset JSON for every models.ini
 *    section (issue #108). The detailed sibling of @c GET /models (which stays a
 *    lightweight discovery list). Per-preset @c load.hfToken is redacted.
 *  - @c GET  /config/models/{name} — single full @c ModelPreset by section name;
 *    404 when unknown. @c load.hfToken redacted.
 *  - @c PUT  /config/models/{name} — upsert a preset from a full @c ModelPreset
 *    JSON body. The path @c {name} overrides the body name so a write cannot
 *    rename. Persists via @c IModelsIni::savePreset (creates if new, overwrites
 *    if existing).
 *  - @c GET  /batches — list all saved batch names (issue #111).
 *  - @c GET  /batches/{name} — the preset section names in a batch; 404 when
 *    unknown.
 *  - @c POST /batches/load — body @c {"batch":"<name>"}; loads each preset in
 * the batch individually (tracker.requestLoad + loadModel), skipping any preset
 *    no longer in models.ini. 400 on missing/empty/unknown batch.
 *  - @c POST /batches/unload — body @c {"batch":"<name>"} unloads each preset in
 *    that batch (@c scope=="batch"). No body / no @c "batch" key → unload ALL
 *    loaded models (@c scope=="all").
 *
 * @param svr  The @c httplib::Server to register handlers on. Must not have
 *             started its accept loop yet.
 * @param deps Dependency bundle; references must outlive @p svr.
 *
 * @pre @c svr.bind_to_port() has returned @c true.
 * @pre @c svr.listen_after_bind() has NOT been called yet.
 */
void registerControlApiRoutes(httplib::Server &svr, const ControlApiDeps &deps);
