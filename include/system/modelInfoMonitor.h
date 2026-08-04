#pragma once

#include "IModelInfoMonitor.h"
#include "llamaServerProcess.h"
#include "modelInfo.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/**
 * @file modelInfoMonitor.h
 * @brief Thread that polls llama-server and feeds the ModelStateTracker.
 *
 * Polls /models at 1Hz to discover loaded models, then per model probes the
 * worker and gathers /slots + /metrics. The raw truth is folded into the
 * ModelStateTracker (the single source of truth for lifecycle); this class no
 * longer owns model state. The IModelInfoMonitor read methods are thin views
 * over the tracker's snapshot, kept so existing panels compile unchanged.
 *
 * Thread-safe: all public methods can be called concurrently.
 */
class ModelInfoMonitor : public IModelInfoMonitor
{
  public:
	/**
	 * @brief Returns the process-wide singleton instance.
	 * @return Reference to the single @c ModelInfoMonitor object.
	 */
	static ModelInfoMonitor &instance()
	{
		static ModelInfoMonitor monitor;
		return monitor;
	}

	/** @brief Starts the background polling thread. */
	void start();

	/** @brief Stops the background polling thread. */
	void stop();

	/**
	 * @brief Aggregate snapshot derived from the tracker (server state + "any
	 * model loaded"). Kept for the few single-model callers during the #110
	 * transition.
	 */
	ModelInfo getStats() const override;

	/**
	 * @brief Stats for one loaded model by id (section name), from the tracker.
	 * @param name Model id / models.ini section name.
	 * @return The model's latest @c ModelInfo, or a default if unknown.
	 */
	ModelInfo getStatsFor(const std::string &name) const override;

	/**
	 * @brief Snapshot of every loaded model's stats, from the tracker.
	 * @return Map of model id to its latest @c ModelInfo.
	 */
	std::map<std::string, ModelInfo> getAllStats() const override;

	/**
	 * @brief Parses Prometheus-style metrics response from llama-server.
	 * @param response Raw response body from /metrics.
	 * @return ModelInfo with parsed values.
	 */
	[[nodiscard]] static ModelInfo
	parseMetricsResponse(const std::string &response);

	/**
	 * @brief Extracts the loaded model id from a /models response body.
	 *
	 * Finds the model entry whose status is "loaded" and returns the nearest
	 * preceding "id" value. Returns "" when none is loaded or the body is
	 * malformed/empty. Reused by LlamaServerProcess::getLoadedModelPath().
	 *
	 * @param response Raw response body from /models.
	 * @return Loaded model id, or "" if none.
	 */
	[[nodiscard]] static std::string
	parseLoadedModelId(const std::string &response);

	/**
	 * @brief Parse all model ids whose status is "loaded" from a /models body.
	 *
	 * Defensive: missing fields, a non-array @c data key, and any JSON parse
	 * failure all yield an empty vector (no exception).
	 *
	 * @param response Raw response body from /models.
	 * @return Ids of every loaded model; empty if none or on failure.
	 */
	[[nodiscard]] static std::vector<std::string>
	parseLoadedModelIds(const std::string &response);

	/**
	 * @brief Largest n_decoded across all slots in a /slots response body.
	 * @param slotJson Raw response body from /slots.
	 * @return Max n_decoded across slots, or 0 if none/parse fails.
	 */
	[[nodiscard]] static int maxDecodedTokens(const std::string &slotJson);

	/**
	 * @brief Consecutive-DEAD crash gate (legacy form, retained for testing).
	 *
	 * The live gate now lives in ModelStateTracker::evaluateProbe; this keeps
	 * the identical semantics for the existing monitor unit tests.
	 *
	 * @param state     Classified result of the latest worker probe.
	 * @param deadCount In/out running count of consecutive DEAD probes.
	 * @return true if the worker should be treated as crashed.
	 */
	[[nodiscard]] static bool
	evaluateProbeResult(LlamaServerProcess::WorkerState state, int &deadCount);

  private:
	ModelInfoMonitor();
	~ModelInfoMonitor();

	ModelInfoMonitor(const ModelInfoMonitor &) = delete;
	ModelInfoMonitor &operator=(const ModelInfoMonitor &) = delete;

	/** @brief Background polling loop. */
	void pollLoop();

	/** @brief Check if any slot is processing a request. */
	bool isProcessing(const std::string &slotJson);

	/** @brief Fetches metrics once from llama-server. */
	ModelInfo fetchMetricsOnce(const std::string &modelName);

	/**
	 * @brief Build one model's ModelInfo from /slots (+ /metrics on edge).
	 *
	 * Queries /slots for @p id, derives activity/active-request counts, and on a
	 * processing->idle edge fetches /metrics once (otherwise carries @p previous
	 * model's token counters forward). Updates @c m_wasProcessing[id]. Poll
	 * thread only; used as the tracker's gatherStats callable.
	 *
	 * @param id       Model id / models.ini section name.
	 * @param previous Last published stats for this model.
	 * @return Fresh @c ModelInfo for the model.
	 */
	ModelInfo pollModel(const std::string &id, const ModelInfo &previous);

	// Thread control. m_stopCv/m_stopMutex let stop() interrupt the poll
	// interval immediately instead of waiting out a full sleep.
	std::atomic<bool> m_running;
	std::thread m_pollThread;
	std::condition_variable m_stopCv;
	std::mutex m_stopMutex;

	// Processing<->idle transition per model id, for the one-shot metrics fetch.
	// Poll-thread-private; never touched by other threads, so it needs no lock.
	std::map<std::string, bool> m_wasProcessing;

	// Last stats published per model id. Poll-thread-private: lets pollModel
	// carry token counters forward between the metrics-fetch edges WITHOUT
	// reading them back from the tracker. Reading the tracker inside the
	// gatherStats callable would re-enter ModelStateTracker::ingestPoll's lock
	// and deadlock (#110, Phase 4a), so the previous-stats source lives here
	// instead.
	std::map<std::string, ModelInfo> m_lastStats;
};
