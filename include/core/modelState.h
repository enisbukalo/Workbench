#pragma once

#include "modelInfo.h"

#include <string>

/**
 * @file modelState.h
 * @brief Per-model lifecycle types owned by ModelStateTracker.
 *
 * Defines the single source-of-truth state machine for a loaded model. Kept in
 * @c include/core so the tracker, the monitor, the UI panel, and the control
 * API can all reference the same vocabulary without pulling in heavier
 * dependencies (HTTP, process handles).
 */

/**
 * @enum ModelLifecycle
 * @brief Where a model is in its load/unload lifecycle.
 *
 * Transitions are driven by two inputs in ModelStateTracker: explicit intent
 * (requestLoad/requestUnload, from the UI or the control API) and observed
 * server truth (ingestPoll, from the monitor). The crash-vs-clean-unload
 * decision is deterministic from the prior lifecycle, which removes the
 * cross-component race that plagued the scattered-state design (#110).
 */
enum class ModelLifecycle
{
	LOADING,   ///< Load requested; not yet confirmed present by the server.
	LOADED,    ///< Present in /models and the worker probe is healthy.
	UNLOADING, ///< Unload requested; disappearance is EXPECTED, not a crash.
	GONE,      ///< Confirmed absent after an intentional unload. Pruned soon.
	CRASHED    ///< Absent/dead WITHOUT a prior unload request; needs recovery.
};

/**
 * @enum WorkerLiveness
 * @brief Liveness of a model's router worker, as seen by a proxied probe.
 *
 * Neutral mirror of LlamaServerProcess::WorkerState so the tracker stays free of
 * process/HTTP dependencies. The monitor maps one onto the other at the poll
 * boundary.
 *
 * ALIVE — worker answered (HTTP 200).
 * BUSY  — reachable but slow (heavy decode); a transient stall, NOT a crash.
 * DEAD  — request refused/closed: the worker process is gone (llama.cpp #18912).
 */
enum class WorkerLiveness
{
	ALIVE,
	BUSY,
	DEAD
};

/**
 * @struct ModelState
 * @brief One model's lifecycle plus its latest stats, keyed by id elsewhere.
 *
 * @c id is the models.ini section name (== the /models id). @c stats carries the
 * throughput/activity surfaced by the info panel. @c deadProbeCount is the
 * running count of consecutive DEAD probes feeding the crash gate; it lives here
 * so the gate has one home per model.
 */
struct ModelState
{
	std::string id;
	ModelLifecycle lifecycle = ModelLifecycle::LOADING;
	ModelInfo stats;
	int deadProbeCount = 0;
};
