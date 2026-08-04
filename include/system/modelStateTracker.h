#pragma once

#include "IModelStateTracker.h"

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

/**
 * @file modelStateTracker.h
 * @brief Thread-safe single source of truth for per-model load/unload state.
 *
 * Owns the lifecycle state machine for every model, the consecutive-DEAD crash
 * gate, and the issue-#71 force-unload-all suppression flag. The monitor feeds
 * it raw poll truth; the UI and control API drive intent and read snapshots.
 *
 * Thread-safety: a single mutex guards the whole record map and both flags. The
 * poll, UI, and control-API threads all enter through it, so state can never
 * desync across components.
 */
class ModelStateTracker : public IModelStateTracker
{
  public:
	/**
	 * @brief Process-wide singleton (Meyers').
	 * @return Reference to the single ModelStateTracker.
	 */
	static ModelStateTracker &instance()
	{
		static ModelStateTracker tracker;
		return tracker;
	}

	ModelStateTracker() = default;
	~ModelStateTracker() override = default;

	ModelStateTracker(const ModelStateTracker &) = delete;
	ModelStateTracker &operator=(const ModelStateTracker &) = delete;

	void requestLoad(const std::string &id) override;
	void requestUnload(const std::string &id) override;
	void requestUnloadAll() override;

	void
	ingestPoll(const std::vector<std::string> &loadedIds,
			   const std::function<WorkerLiveness(const std::string &)> &probe,
			   const std::function<ModelInfo(const std::string &)> &gatherStats)
		override;

	void onServerOffline() override;

	[[nodiscard]] std::map<std::string, ModelState> snapshot() const override;
	[[nodiscard]] bool shouldSkipModelQueries() const override;
	[[nodiscard]] std::vector<std::string> takeCrashed() override;

	/**
	 * @brief Consecutive-DEAD crash gate, pure and stateless apart from @p
	 * count.
	 *
	 * ALIVE resets the streak; BUSY is neutral (reachable, neither advances nor
	 * clears); DEAD advances and, on reaching @c kProbeFailThreshold, certifies
	 * a crash and resets. Exposed static for unit testing and reuse.
	 *
	 * @param liveness Latest probe classification.
	 * @param count    In/out running DEAD streak for one model.
	 * @return true when the worker should be treated as crashed.
	 */
	[[nodiscard]] static bool evaluateProbe(WorkerLiveness liveness, int &count);

	/// Consecutive DEAD probes that certify a crash (matches the prior monitor).
	static constexpr int kProbeFailThreshold = 3;

  private:
	mutable std::mutex m_mutex;

	// All per-model state, keyed by id (== models.ini section name).
	std::map<std::string, ModelState> m_models;

	// Ids that entered CRASHED and have not yet been drained by takeCrashed().
	std::set<std::string> m_crashed;

	// Issue #71: skip model-specific polling after an unload-all so the router
	// does not auto-reload on the next /models or /slots query.
	bool m_skipModelQueries = false;
};
