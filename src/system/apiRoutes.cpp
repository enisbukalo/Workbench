/**
 * @file apiRoutes.cpp
 * @brief Control-API route handler implementations (issues #106, #107).
 *
 * Registers all control-API endpoints and provides the shared
 * @c isAuthorized() guard used by all control-API handlers.
 *
 * This translation unit depends only on the I* interfaces; it never calls
 * singleton ::instance() methods directly. Singleton wiring lives in
 * controlApiServer.cpp so this module stays unit-testable via mocks.
 */

#include "apiRoutes.h"

#include "httplib.h"
#include "json.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>

// ---------------------------------------------------------------------------
// isAuthorized
// ---------------------------------------------------------------------------

bool isAuthorized(const httplib::Request &req,
				  const Config::ApiSettings &settings)
{
	if (!settings.apiRequireKey)
		return true;

	// Expect "Authorization: Bearer <key>"
	auto it = req.headers.find("Authorization");
	if (it == req.headers.end())
		return false;

	constexpr std::string_view kPrefix = "Bearer ";
	const std::string &headerValue = it->second;

	if (headerValue.size() <= kPrefix.size())
		return false;

	if (std::string_view{ headerValue }.substr(0, kPrefix.size()) != kPrefix)
		return false;

	const std::string presented = headerValue.substr(kPrefix.size());
	return presented == settings.apiKey;
}

// ---------------------------------------------------------------------------
// Secret redaction helpers (issue #108)
// ---------------------------------------------------------------------------

namespace {

/// Sentinel substituted for non-empty secret strings in config responses.
constexpr const char *kRedacted = "***REDACTED***";

/// Redact one string field at j[key] if present and non-empty. Empty/absent
/// values are left untouched so a reader can tell "set but hidden" from "unset".
void redactField(nlohmann::json &j, const char *key)
{
	if (j.is_object() && j.contains(key) && j[key].is_string() &&
		!j[key].get<std::string>().empty())
		j[key] = kRedacted;
}

/// Redact secrets in a serialized UserConfig: server.apiKey, server.apiKeyFile,
/// server.sslKeyFile, api.apiKey. Every access is guarded so a missing
/// sub-object never throws.
void redactConfigSecrets(nlohmann::json &cfg)
{
	if (cfg.contains("server") && cfg["server"].is_object()) {
		redactField(cfg["server"], "apiKey");
		redactField(cfg["server"], "apiKeyFile");
		redactField(cfg["server"], "sslKeyFile");
	}
	if (cfg.contains("api") && cfg["api"].is_object())
		redactField(cfg["api"], "apiKey");
}

/// Redact secrets in a serialized ModelPreset: load.hfToken.
void redactPresetSecrets(nlohmann::json &preset)
{
	if (preset.contains("load") && preset["load"].is_object())
		redactField(preset["load"], "hfToken");
}

} // namespace

// ---------------------------------------------------------------------------
// registerControlApiRoutes
// ---------------------------------------------------------------------------

void registerControlApiRoutes(httplib::Server &svr, const ControlApiDeps &deps)
{
	// POST /server/restart
	// Mirrors ModelsPanel::restartServer():
	//   1. tracker.requestUnloadAll() to suppress router auto-reload (issue #71)
	//   2. terminate()
	//   3. launch("", serverSettings)
	svr.Post(
		"/server/restart",
		[deps](const httplib::Request &req, httplib::Response &res) {
			try {
				if (!isAuthorized(req, deps.apiSettings)) {
					res.status = 401;
					res.set_content(R"({"error":"unauthorized"})",
									"application/json");
					return;
				}

				spdlog::info("ControlApiServer: POST /server/restart received");

				// Drop all model state + suppress auto-reload (issue #71) before
				// relaunching empty.
				deps.tracker.requestUnloadAll();
				deps.server.terminate();

				const Config::ServerSettings serverSettings =
					deps.config.getServerSettings();
				const bool ok = deps.server.launch("", serverSettings);

				if (ok) {
					spdlog::info("ControlApiServer: llama-server relaunched "
								 "successfully");
				} else {
					spdlog::warn(
						"ControlApiServer: llama-server relaunch failed");
				}

				nlohmann::json body;
				body["status"] = ok ? "restarted" : "failed";
				body["running"] = deps.server.isRunning();

				res.status = ok ? 200 : 500;
				res.set_content(body.dump(), "application/json");
			} catch (const std::exception &e) {
				spdlog::error("ControlApiServer: POST /server/restart threw: {}",
							  e.what());
				nlohmann::json errBody;
				errBody["error"] = e.what();
				res.status = 500;
				res.set_content(errBody.dump(), "application/json");
			}
		});

	// GET /models
	// Returns all presets from models.ini as a JSON array, surfacing a useful
	// subset of each preset's load and inference parameters (issue #107).
	svr.Get("/models",
			[deps](const httplib::Request &req, httplib::Response &res) {
				try {
					if (!isAuthorized(req, deps.apiSettings)) {
						res.status = 401;
						res.set_content(R"({"error":"unauthorized"})",
										"application/json");
						return;
					}

					spdlog::info("ControlApiServer: GET /models received");

					nlohmann::json arr = nlohmann::json::array();

					for (const std::string &name : deps.models.getModelNames()) {
						const auto preset = deps.models.getPreset(name);
						if (!preset.has_value())
							continue;

						// Prefer load.modelPath; fall back to the top-level
						// model field.
						const std::string &modelPath =
							preset->load.modelPath.empty()
								? preset->model
								: preset->load.modelPath;

						nlohmann::json entry;
						entry["name"] = name;
						entry["model"] = modelPath;

						nlohmann::json load;
						load["ctxSize"] = preset->load.ctxSize;
						load["ngpuLayers"] = preset->load.ngpuLayers;
						load["batchSize"] = preset->load.batchSize;
						load["parallel"] = preset->load.parallel;
						load["flashAttn"] = preset->load.flashAttn;
						load["cacheTypeK"] = preset->load.cacheTypeK;
						load["cacheTypeV"] = preset->load.cacheTypeV;
						entry["load"] = std::move(load);

						nlohmann::json inference;
						inference["temperature"] = preset->inference.temperature;
						inference["topK"] = preset->inference.topK;
						inference["topP"] = preset->inference.topP;
						inference["nPredict"] = preset->inference.nPredict;
						entry["inference"] = std::move(inference);

						arr.push_back(std::move(entry));
					}

					nlohmann::json body;
					body["models"] = std::move(arr);

					res.status = 200;
					res.set_content(body.dump(), "application/json");
				} catch (const std::exception &e) {
					spdlog::error("ControlApiServer: GET /models threw: {}",
								  e.what());
					nlohmann::json errBody;
					errBody["error"] = e.what();
					res.status = 500;
					res.set_content(errBody.dump(), "application/json");
				}
			});

	// POST /models/load
	// Body: { "model": "<preset/section name>" }
	// Mirrors ModelsPanel load ordering (issue #107):
	//   1. tracker.requestLoad(name) — record intent, clear the skip flag
	//   2. loadModel(name)           — trigger llama-server hot-swap
	svr.Post(
		"/models/load",
		[deps](const httplib::Request &req, httplib::Response &res) {
			try {
				if (!isAuthorized(req, deps.apiSettings)) {
					res.status = 401;
					res.set_content(R"({"error":"unauthorized"})",
									"application/json");
					return;
				}

				spdlog::info("ControlApiServer: POST /models/load received");

				// Parse and validate request body.
				const nlohmann::json parsed =
					nlohmann::json::parse(req.body,
										  nullptr,
										  /*exceptions=*/false);
				if (parsed.is_discarded() || !parsed.contains("model") ||
					!parsed["model"].is_string()) {
					res.status = 400;
					res.set_content(
						R"({"error":"request body must be JSON with a string \"model\" field"})",
						"application/json");
					return;
				}

				const std::string name = parsed["model"].get<std::string>();
				if (name.empty()) {
					res.status = 400;
					res.set_content(
						R"({"error":"\"model\" field must not be empty"})",
						"application/json");
					return;
				}

				// Validate the name against the known preset list.
				const auto names = deps.models.getModelNames();
				const bool known =
					std::find(names.begin(), names.end(), name) != names.end();
				if (!known) {
					nlohmann::json errBody;
					errBody["error"] = "unknown model preset: " + name;
					res.status = 400;
					res.set_content(errBody.dump(), "application/json");
					return;
				}

				// Record load intent (LOADING; resumes polling) then trigger the
				// hot-swap. The monitor confirms LOADED on the next poll.
				deps.tracker.requestLoad(name);
				const bool ok = deps.server.loadModel(name);

				if (ok) {
					spdlog::info("ControlApiServer: loadModel(\"{}\") accepted",
								 name);
				} else {
					spdlog::warn("ControlApiServer: loadModel(\"{}\") failed",
								 name);
				}

				nlohmann::json body;
				body["status"] = ok ? "loading" : "failed";
				body["model"] = name;
				body["loaded"] = deps.server.isModelLoaded();

				res.status = ok ? 200 : 502;
				res.set_content(body.dump(), "application/json");
			} catch (const std::exception &e) {
				spdlog::error("ControlApiServer: POST /models/load threw: {}",
							  e.what());
				nlohmann::json errBody;
				errBody["error"] = e.what();
				res.status = 500;
				res.set_content(errBody.dump(), "application/json");
			}
		});

	// POST /models/unload
	// Body-driven: optional JSON body selects between two modes.
	//
	// Mode 1 — unload ALL (no body, missing "model" key, or unparseable body):
	//   {}  or  <empty>
	//   tracker.requestUnloadAll() then unloadAllModels(); response
	//   scope=="all".
	//
	// Mode 2 — unload ONE (body carries a non-empty string "model" field):
	//   { "model": "<models.ini section name>" }
	//   Validates the name; tracker.requestUnload(name) then unloadModel(name);
	//   response scope=="model".
	//
	// Intent is recorded on the shared ModelStateTracker so the UI and the API
	// drive the same state machine (#110): requestUnloadAll suppresses the
	// router auto-reload (issue #71); requestUnload marks a single model's
	// coming disappearance as expected, so the UI crash-detect never restarts
	// the server when one of several models is dropped. Intent is recorded only
	// after the request validates, so a 400 leaves tracker state untouched.
	svr.Post(
		"/models/unload",
		[deps](const httplib::Request &req, httplib::Response &res) {
			try {
				if (!isAuthorized(req, deps.apiSettings)) {
					res.status = 401;
					res.set_content(R"({"error":"unauthorized"})",
									"application/json");
					return;
				}

				spdlog::info("ControlApiServer: POST /models/unload received");

				const auto parsed = nlohmann::json::parse(req.body,
														  nullptr,
														  /*exceptions=*/false);

				const bool hasModelField =
					!parsed.is_discarded() && parsed.contains("model");

				if (!hasModelField) {
					// No body / no "model" key — unload ALL loaded models.
					deps.tracker.requestUnloadAll();
					spdlog::info("ControlApiServer: unloading all models");
					const bool ok = deps.server.unloadAllModels();

					if (ok) {
						spdlog::info(
							"ControlApiServer: unloadAllModels() succeeded");
					} else {
						spdlog::warn(
							"ControlApiServer: unloadAllModels() failed");
					}

					nlohmann::json body;
					body["status"] = ok ? "unloaded" : "failed";
					body["scope"] = "all";
					body["loaded"] = false;

					res.status = ok ? 200 : 500;
					res.set_content(body.dump(), "application/json");
					return;
				}

				// "model" key is present — validate and unload the named model.
				if (!parsed["model"].is_string() ||
					parsed["model"].get<std::string>().empty()) {
					res.status = 400;
					res.set_content(
						R"({"error":"\"model\" field must be a non-empty string"})",
						"application/json");
					return;
				}

				const std::string name = parsed["model"].get<std::string>();

				// Validate the name against the known preset list.
				const auto names = deps.models.getModelNames();
				const bool known =
					std::find(names.begin(), names.end(), name) != names.end();
				if (!known) {
					nlohmann::json errBody;
					errBody["error"] = "unknown model preset: " + name;
					res.status = 400;
					res.set_content(errBody.dump(), "application/json");
					return;
				}

				// Record single-model unload intent so its disappearance is
				// expected, not a crash (#110).
				deps.tracker.requestUnload(name);
				spdlog::info("ControlApiServer: unloading model '{}'", name);
				const bool ok = deps.server.unloadModel(name);

				if (ok) {
					spdlog::info(
						"ControlApiServer: unloadModel(\"{}\") succeeded",
						name);
				} else {
					spdlog::warn("ControlApiServer: unloadModel(\"{}\") failed",
								 name);
				}

				nlohmann::json body;
				body["status"] = ok ? "unloaded" : "failed";
				body["scope"] = "model";
				body["model"] = name;
				body["loaded"] = false;

				res.status = ok ? 200 : 500;
				res.set_content(body.dump(), "application/json");
			} catch (const std::exception &e) {
				spdlog::error("ControlApiServer: POST /models/unload threw: {}",
							  e.what());
				nlohmann::json errBody;
				errBody["error"] = e.what();
				res.status = 500;
				res.set_content(errBody.dump(), "application/json");
			}
		});

	// GET /config/app
	// READ-ONLY app/server/api/ui/terminal/discovery settings (issue #108).
	// Secrets are redacted before responding. There is intentionally NO write
	// counterpart: most app settings only take effect on app restart, so a live
	// write would mislead.
	svr.Get("/config/app",
			[deps](const httplib::Request &req, httplib::Response &res) {
				try {
					if (!isAuthorized(req, deps.apiSettings)) {
						res.status = 401;
						res.set_content(R"({"error":"unauthorized"})",
										"application/json");
						return;
					}

					spdlog::info("ControlApiServer: GET /config/app received");

					nlohmann::json body = deps.config.getConfigSnapshot();
					redactConfigSecrets(body);

					res.status = 200;
					res.set_content(body.dump(), "application/json");
				} catch (const std::exception &e) {
					spdlog::error("ControlApiServer: GET /config/app threw: {}",
								  e.what());
					nlohmann::json errBody;
					errBody["error"] = e.what();
					res.status = 500;
					res.set_content(errBody.dump(), "application/json");
				}
			});

	// GET /config/models
	// Full ModelPreset JSON for every models.ini section (issue #108). The
	// detailed sibling of GET /models. Per-preset load.hfToken is redacted.
	// Registered BEFORE the /config/models/{name} regex so the bare path is
	// matched by this literal handler.
	svr.Get(
		"/config/models",
		[deps](const httplib::Request &req, httplib::Response &res) {
			try {
				if (!isAuthorized(req, deps.apiSettings)) {
					res.status = 401;
					res.set_content(R"({"error":"unauthorized"})",
									"application/json");
					return;
				}

				spdlog::info("ControlApiServer: GET /config/models received");

				nlohmann::json arr = nlohmann::json::array();
				for (const std::string &name : deps.models.getModelNames()) {
					const auto preset = deps.models.getPreset(name);
					if (!preset.has_value())
						continue;
					nlohmann::json entry = *preset;
					redactPresetSecrets(entry);
					arr.push_back(std::move(entry));
				}

				nlohmann::json body;
				body["models"] = std::move(arr);

				res.status = 200;
				res.set_content(body.dump(), "application/json");
			} catch (const std::exception &e) {
				spdlog::error("ControlApiServer: GET /config/models threw: {}",
							  e.what());
				nlohmann::json errBody;
				errBody["error"] = e.what();
				res.status = 500;
				res.set_content(errBody.dump(), "application/json");
			}
		});

	// GET /config/models/{name}
	// Single full ModelPreset by section name; 404 when unknown (issue #108).
	// httplib URL-decodes the path capture group into req.matches[1].
	svr.Get(
		R"(/config/models/([^/]+))",
		[deps](const httplib::Request &req, httplib::Response &res) {
			try {
				if (!isAuthorized(req, deps.apiSettings)) {
					res.status = 401;
					res.set_content(R"({"error":"unauthorized"})",
									"application/json");
					return;
				}

				const std::string name = req.matches[1];
				spdlog::info("ControlApiServer: GET /config/models/{} received",
							 name);

				const auto preset = deps.models.getPreset(name);
				if (!preset.has_value()) {
					nlohmann::json errBody;
					errBody["error"] = "unknown model preset: " + name;
					res.status = 404;
					res.set_content(errBody.dump(), "application/json");
					return;
				}

				nlohmann::json body = *preset;
				redactPresetSecrets(body);

				res.status = 200;
				res.set_content(body.dump(), "application/json");
			} catch (const std::exception &e) {
				spdlog::error(
					"ControlApiServer: GET /config/models/{{name}} threw: {}",
					e.what());
				nlohmann::json errBody;
				errBody["error"] = e.what();
				res.status = 500;
				res.set_content(errBody.dump(), "application/json");
			}
		});

	// PUT /config/models/{name}
	// Upsert a preset from a full ModelPreset JSON body (issue #108). The path
	// {name} overrides the body name so a write cannot rename/escape. Persists
	// via savePreset: creates the section if new, overwrites if existing.
	svr.Put(
		R"(/config/models/([^/]+))",
		[deps](const httplib::Request &req, httplib::Response &res) {
			try {
				if (!isAuthorized(req, deps.apiSettings)) {
					res.status = 401;
					res.set_content(R"({"error":"unauthorized"})",
									"application/json");
					return;
				}

				const std::string name = req.matches[1];
				spdlog::info("ControlApiServer: PUT /config/models/{} received",
							 name);

				const nlohmann::json parsed =
					nlohmann::json::parse(req.body,
										  nullptr,
										  /*exceptions=*/false);
				if (parsed.is_discarded() || !parsed.is_object()) {
					res.status = 400;
					res.set_content(
						R"({"error":"request body must be a JSON object"})",
						"application/json");
					return;
				}

				Config::ModelPreset preset;
				try {
					preset = parsed.get<Config::ModelPreset>();
				} catch (const std::exception &e) {
					nlohmann::json errBody;
					errBody["error"] =
						std::string("malformed preset: ") + e.what();
					res.status = 400;
					res.set_content(errBody.dump(), "application/json");
					return;
				}

				// Path name wins -- the body cannot rename the section.
				preset.name = name;
				preset.validate();

				const bool ok = deps.models.savePreset(preset);

				if (ok) {
					spdlog::info(
						"ControlApiServer: savePreset(\"{}\") succeeded",
						name);
				} else {
					spdlog::warn("ControlApiServer: savePreset(\"{}\") failed",
								 name);
				}

				nlohmann::json body;
				body["status"] = ok ? "saved" : "failed";
				body["model"] = name;

				res.status = ok ? 200 : 500;
				res.set_content(body.dump(), "application/json");
			} catch (const std::exception &e) {
				spdlog::error(
					"ControlApiServer: PUT /config/models/{{name}} threw: {}",
					e.what());
				nlohmann::json errBody;
				errBody["error"] = e.what();
				res.status = 500;
				res.set_content(errBody.dump(), "application/json");
			}
		});

	// GET /batches
	// List all saved batch names (issue #111). Registered BEFORE the
	// /batches/{name} regex so the bare path matches this literal handler.
	svr.Get("/batches",
			[deps](const httplib::Request &req, httplib::Response &res) {
				try {
					if (!isAuthorized(req, deps.apiSettings)) {
						res.status = 401;
						res.set_content(R"({"error":"unauthorized"})",
										"application/json");
						return;
					}

					spdlog::info("ControlApiServer: GET /batches received");

					nlohmann::json body;
					body["batches"] = deps.batches.getBatchNames();
					res.status = 200;
					res.set_content(body.dump(), "application/json");
				} catch (const std::exception &e) {
					spdlog::error("ControlApiServer: GET /batches threw: {}",
								  e.what());
					nlohmann::json errBody;
					errBody["error"] = e.what();
					res.status = 500;
					res.set_content(errBody.dump(), "application/json");
				}
			});

	// GET /batches/{name}
	// The preset section names in a batch; 404 when unknown (issue #111).
	svr.Get(R"(/batches/([^/]+))",
			[deps](const httplib::Request &req, httplib::Response &res) {
				try {
					if (!isAuthorized(req, deps.apiSettings)) {
						res.status = 401;
						res.set_content(R"({"error":"unauthorized"})",
										"application/json");
						return;
					}

					const std::string name = req.matches[1];
					spdlog::info("ControlApiServer: GET /batches/{} received",
								 name);

					const auto batch = deps.batches.getBatch(name);
					if (!batch.has_value()) {
						nlohmann::json errBody;
						errBody["error"] = "unknown batch: " + name;
						res.status = 404;
						res.set_content(errBody.dump(), "application/json");
						return;
					}

					nlohmann::json body;
					body["batch"] = batch->name;
					body["presets"] = batch->presets;
					res.status = 200;
					res.set_content(body.dump(), "application/json");
				} catch (const std::exception &e) {
					spdlog::error(
						"ControlApiServer: GET /batches/{{name}} threw: {}",
						e.what());
					nlohmann::json errBody;
					errBody["error"] = e.what();
					res.status = 500;
					res.set_content(errBody.dump(), "application/json");
				}
			});

	// POST /batches/load
	// Body: { "batch": "<name>" }. Loads every preset in the batch individually,
	// mirroring /models/load ordering (tracker.requestLoad then loadModel).
	// Presets not present in models.ini are skipped (a batch may reference a
	// since-deleted preset).
	svr.Post(
		"/batches/load",
		[deps](const httplib::Request &req, httplib::Response &res) {
			try {
				if (!isAuthorized(req, deps.apiSettings)) {
					res.status = 401;
					res.set_content(R"({"error":"unauthorized"})",
									"application/json");
					return;
				}

				spdlog::info("ControlApiServer: POST /batches/load received");

				const nlohmann::json parsed =
					nlohmann::json::parse(req.body,
										  nullptr,
										  /*exceptions=*/false);
				if (parsed.is_discarded() || !parsed.contains("batch") ||
					!parsed["batch"].is_string() ||
					parsed["batch"].get<std::string>().empty()) {
					res.status = 400;
					res.set_content(
						R"({"error":"request body must be JSON with a non-empty string \"batch\" field"})",
						"application/json");
					return;
				}

				const std::string name = parsed["batch"].get<std::string>();
				const auto batch = deps.batches.getBatch(name);
				if (!batch.has_value()) {
					nlohmann::json errBody;
					errBody["error"] = "unknown batch: " + name;
					res.status = 400;
					res.set_content(errBody.dump(), "application/json");
					return;
				}

				const auto known = deps.models.getModelNames();
				nlohmann::json loaded = nlohmann::json::array();
				nlohmann::json skipped = nlohmann::json::array();
				for (const auto &section : batch->presets) {
					if (std::find(known.begin(), known.end(), section) ==
						known.end()) {
						spdlog::warn("Batch '{}' references unknown preset "
									 "'{}', skipping",
									 name,
									 section);
						skipped.push_back(section);
						continue;
					}
					deps.tracker.requestLoad(section);
					if (deps.server.loadModel(section))
						loaded.push_back(section);
					else
						skipped.push_back(section);
				}

				nlohmann::json body;
				body["status"] = "loaded";
				body["batch"] = name;
				body["loaded"] = std::move(loaded);
				body["skipped"] = std::move(skipped);
				res.status = 200;
				res.set_content(body.dump(), "application/json");
			} catch (const std::exception &e) {
				spdlog::error("ControlApiServer: POST /batches/load threw: {}",
							  e.what());
				nlohmann::json errBody;
				errBody["error"] = e.what();
				res.status = 500;
				res.set_content(errBody.dump(), "application/json");
			}
		});

	// POST /batches/unload
	// Body: { "batch": "<name>" } unloads each preset in that batch. No body /
	// no "batch" key → unload ALL loaded models (mirrors /models/unload).
	svr.Post(
		"/batches/unload",
		[deps](const httplib::Request &req, httplib::Response &res) {
			try {
				if (!isAuthorized(req, deps.apiSettings)) {
					res.status = 401;
					res.set_content(R"({"error":"unauthorized"})",
									"application/json");
					return;
				}

				spdlog::info("ControlApiServer: POST /batches/unload received");

				const auto parsed = nlohmann::json::parse(req.body,
														  nullptr,
														  /*exceptions=*/false);
				const bool hasBatchField =
					!parsed.is_discarded() && parsed.contains("batch");

				if (!hasBatchField) {
					// No body / no "batch" key — unload ALL loaded models.
					deps.tracker.requestUnloadAll();
					spdlog::info("ControlApiServer: unloading all models");
					const bool ok = deps.server.unloadAllModels();

					nlohmann::json body;
					body["status"] = ok ? "unloaded" : "failed";
					body["scope"] = "all";
					res.status = ok ? 200 : 500;
					res.set_content(body.dump(), "application/json");
					return;
				}

				if (!parsed["batch"].is_string() ||
					parsed["batch"].get<std::string>().empty()) {
					res.status = 400;
					res.set_content(
						R"({"error":"\"batch\" field must be a non-empty string"})",
						"application/json");
					return;
				}

				const std::string name = parsed["batch"].get<std::string>();
				const auto batch = deps.batches.getBatch(name);
				if (!batch.has_value()) {
					nlohmann::json errBody;
					errBody["error"] = "unknown batch: " + name;
					res.status = 400;
					res.set_content(errBody.dump(), "application/json");
					return;
				}

				nlohmann::json unloaded = nlohmann::json::array();
				for (const auto &section : batch->presets) {
					// Per-section unload — the batch may be a subset of what is
					// loaded, so never requestUnloadAll here.
					deps.tracker.requestUnload(section);
					if (deps.server.unloadModel(section))
						unloaded.push_back(section);
				}

				nlohmann::json body;
				body["status"] = "unloaded";
				body["scope"] = "batch";
				body["batch"] = name;
				body["unloaded"] = std::move(unloaded);
				res.status = 200;
				res.set_content(body.dump(), "application/json");
			} catch (const std::exception &e) {
				spdlog::error("ControlApiServer: POST /batches/unload threw: {}",
							  e.what());
				nlohmann::json errBody;
				errBody["error"] = e.what();
				res.status = 500;
				res.set_content(errBody.dump(), "application/json");
			}
		});
}
