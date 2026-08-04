#pragma once

#include "modelInfo.h"
#include "modelState.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

/**
 * @file IModelStateTracker.h
 * @brief Interface for the single source of truth of model load/unload state.
 *
 * One owner of per-model lifecycle. The UI and the control API both express
 * intent through it (requestLoad/requestUnload) and read state from it
 * (snapshot); the monitor feeds raw server truth (ingestPoll). Crash detection
 * and the issue-#71 force-unload suppression live here, so no two components
 * keep their own copy of the state — the desync class of bugs that multi-model
 * (#110) exposed cannot occur.
 *
 * Implementations must be thread-safe: the poll thread, the UI thread, and the
 * control-API thread all touch it concurrently.
 */
class IModelStateTracker
{
  public:
	virtual ~IModelStateTracker() = default;

	// --- Intent (UI + control API) -----------------------------------------

	/** @brief Record that a load of @p id was requested (-> LOADING). Resumes
	 *  monitor polling (clears the force-unload-all suppression). */
	virtual void requestLoad(const std::string &id) = 0;

	/** @brief Record that an unload of @p id was requested (-> UNLOADING) so its
	 *  later disappearance is treated as expected, not a crash. */
	virtual void requestUnload(const std::string &id) = 0;

	/** @brief Record that ALL models are being unloaded. Clears tracked state and
	 *  suppresses model-specific polling until the next load (issue #71). */
	virtual void requestUnloadAll() = 0;

	// --- Poll truth (monitor) ----------------------------------------------

	/**
	 * @brief Fold one poll's server truth into the state machine.
	 *
	 * For every id in @p loadedIds and every currently-tracked id, the tracker
	 * applies @p probe (worker liveness) and, for live ones, @p gatherStats, then
	 * advances each model's lifecycle. The callables keep all HTTP/process work in
	 * the caller (monitor), leaving the tracker pure and unit-testable offline.
	 *
	 * @param loadedIds   Ids the server currently reports as loaded.
	 * @param probe       id -> worker liveness (proxied probe result).
	 * @param gatherStats id -> latest ModelInfo (slot status + metrics).
	 */
	virtual void
	ingestPoll(const std::vector<std::string> &loadedIds,
			   const std::function<WorkerLiveness(const std::string &)> &probe,
			   const std::function<ModelInfo(const std::string &)>
				   &gatherStats) = 0;

	/** @brief The server is offline: drop all tracked models. */
	virtual void onServerOffline() = 0;

	// --- Reads (UI + control API) ------------------------------------------

	/** @brief Thread-safe copy of every tracked model's state, keyed by id. */
	[[nodiscard]] virtual std::map<std::string, ModelState>
	snapshot() const = 0;

	/** @brief True when the monitor should skip model-specific queries to avoid
	 *  the router auto-reload (issue #71) — set after an unload-all. */
	[[nodiscard]] virtual bool shouldSkipModelQueries() const = 0;

	/** @brief Return and clear the ids that entered CRASHED since the last call.
	 *  The UI acts on these (server restart); draining ensures one action per
	 *  crash. */
	[[nodiscard]] virtual std::vector<std::string> takeCrashed() = 0;
};
