/**
 * @file modelStateTracker.cpp
 * @brief Implementation of the model lifecycle state machine.
 *
 * See modelStateTracker.h. The crash-vs-clean-unload decision is deterministic
 * from each model's prior lifecycle, which is what removes the cross-component
 * race of the old scattered-state design (#110): an intentional unload sets
 * UNLOADING, and a disappearance from that state becomes GONE — never CRASHED —
 * no matter which thread observes it or when.
 */

#include "modelStateTracker.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <utility>

void ModelStateTracker::requestLoad(const std::string &id)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	// A fresh load resumes polling and starts (or restarts) this model's
	// lifecycle. Any prior GONE/CRASHED record for the id is replaced so a later
	// real crash after this load is detectable again.
	m_skipModelQueries = false;
	auto &state = m_models[id];
	state.id = id;
	state.lifecycle = ModelLifecycle::LOADING;
	state.deadProbeCount = 0;
	m_crashed.erase(id);
}

void ModelStateTracker::requestUnload(const std::string &id)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	auto it = m_models.find(id);
	if (it == m_models.end())
		return;
	// Mark intent: the model's eventual disappearance is now expected.
	it->second.lifecycle = ModelLifecycle::UNLOADING;
}

void ModelStateTracker::requestUnloadAll()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	// Nothing remains loaded after this, so suppress model-specific polling to
	// avoid the router auto-reload (issue #71), and drop all tracked state. No
	// crash is implied by an intentional unload-all.
	m_skipModelQueries = true;
	m_models.clear();
	m_crashed.clear();
}

void ModelStateTracker::onServerOffline()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_models.clear();
	m_crashed.clear();
	// Leave m_skipModelQueries as-is: an offline server says nothing about the
	// user's load/unload intent. It is cleared by the next requestLoad.
}

void ModelStateTracker::ingestPoll(
	const std::vector<std::string> &loadedIds,
	const std::function<WorkerLiveness(const std::string &)> &probe,
	const std::function<ModelInfo(const std::string &)> &gatherStats)
{
	// PHASE A — Lockless gather (NO m_mutex held).
	//
	// The probe/gatherStats callables do blocking HTTP (/slots, /metrics, the
	// proxied worker probe). Holding the tracker lock across that I/O is the
	// Phase-4a bug: it (1) blocks UI/API intent calls on m_mutex for the whole
	// round-trip and (2) self-deadlocks if a callable re-enters the tracker
	// (e.g. snapshot()) on the same non-recursive mutex. So all network happens
	// here, with no lock, into local maps. The callables must not call back into
	// any IModelStateTracker method — the contract that keeps this safe.
	std::map<std::string, WorkerLiveness> liveness;
	std::map<std::string, ModelInfo> stats;
	for (const auto &id : loadedIds) {
		const WorkerLiveness live = probe(id);
		liveness[id] = live;
		// Gather stats for reachable workers (ALIVE always; BUSY only matters
		// for an already-LOADED model, but we cannot know its lifecycle without
		// the lock — gather unconditionally and let the apply phase decide
		// whether to keep it). DEAD workers yield no useful stats; skip the
		// round-trip.
		if (live != WorkerLiveness::DEAD)
			stats[id] = gatherStats(id);
	}

	// PHASE B — Locked apply (m_mutex held, NO I/O). Pure in-memory folding of
	// the gathered liveness/stats: the dead-probe gate, lifecycle transitions,
	// and the "no longer present" sweep. Returns fast.
	std::lock_guard<std::mutex> lock(m_mutex);

	std::set<std::string> present(loadedIds.begin(), loadedIds.end());

	// 1) Fold every id the server currently reports as loaded.
	for (const auto &id : loadedIds) {
		auto &state = m_models[id];
		state.id = id;

		const WorkerLiveness live = liveness[id];
		const bool crashed = evaluateProbe(live, state.deadProbeCount);

		if (crashed) {
			// Reported loaded but the worker is gone (#18912). Unless the user
			// asked to unload it, this is a crash.
			if (state.lifecycle == ModelLifecycle::UNLOADING) {
				state.lifecycle = ModelLifecycle::GONE;
			} else {
				state.lifecycle = ModelLifecycle::CRASHED;
				m_crashed.insert(id);
				spdlog::warn(
					"ModelStateTracker: model '{}' reported loaded but worker "
					"probe DEAD {} times - treating as crashed",
					id,
					kProbeFailThreshold);
			}
			continue;
		}

		// Worker reachable (ALIVE) or merely BUSY: settle the lifecycle and
		// apply the gathered stats. BUSY keeps LOADING warming rather than
		// flipping to LOADED on a stall, but never demotes an already-LOADED
		// model.
		if (live == WorkerLiveness::ALIVE) {
			auto sit = stats.find(id);
			if (sit != stats.end())
				state.stats = sit->second;
			if (state.lifecycle != ModelLifecycle::UNLOADING)
				state.lifecycle = ModelLifecycle::LOADED;
		} else { // BUSY
			if (state.lifecycle == ModelLifecycle::LOADED) {
				auto sit = stats.find(id);
				if (sit != stats.end())
					state.stats = sit->second;
			}
		}
	}

	// 2) Handle ids that are tracked but NO LONGER present server-side.
	for (auto it = m_models.begin(); it != m_models.end();) {
		const std::string &id = it->first;
		if (present.count(id)) {
			++it;
			continue;
		}

		switch (it->second.lifecycle) {
		case ModelLifecycle::UNLOADING:
		case ModelLifecycle::GONE:
			// Intentional unload confirmed gone: drop it. No crash.
			it = m_models.erase(it);
			break;
		case ModelLifecycle::LOADING:
			// Requested but not up yet: keep waiting for it to appear.
			++it;
			break;
		case ModelLifecycle::LOADED:
			// Was loaded, now vanished with no unload request: a crash.
			spdlog::warn("ModelStateTracker: model '{}' was loaded but "
						 "disappeared from /models - treating as crashed",
						 id);
			it->second.lifecycle = ModelLifecycle::CRASHED;
			m_crashed.insert(id);
			++it;
			break;
		case ModelLifecycle::CRASHED:
			// Already recorded; drop now that it is gone.
			it = m_models.erase(it);
			break;
		}
	}
}

std::map<std::string, ModelState> ModelStateTracker::snapshot() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_models;
}

bool ModelStateTracker::shouldSkipModelQueries() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_skipModelQueries;
}

std::vector<std::string> ModelStateTracker::takeCrashed()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	std::vector<std::string> out(m_crashed.begin(), m_crashed.end());
	m_crashed.clear();
	return out;
}

bool ModelStateTracker::evaluateProbe(WorkerLiveness liveness, int &count)
{
	switch (liveness) {
	case WorkerLiveness::ALIVE:
		count = 0;
		return false;
	case WorkerLiveness::BUSY:
		// Reachable but slow: neither a death nor proof of life. Leave the
		// streak so a genuine crash that follows still accumulates.
		return false;
	case WorkerLiveness::DEAD:
		if (++count >= kProbeFailThreshold) {
			count = 0;
			return true;
		}
		return false;
	}
	return false;
}
