/**
 * @file modelInfoMonitor.cpp
 * @brief Implementation of ModelInfoMonitor singleton.
 *
 * Polls /slots endpoint at 1Hz to detect state changes.
 * On transition from processing to idle, queries /metrics once.
 * Thread-safe via mutex-protected access.
 */

#include "modelInfoMonitor.h"
#include "configManager.h"
#include "httpClient.h"
#include "json.hpp"
#include "llamaServerProcess.h"
#include "modelStateTracker.h"

#include <spdlog/spdlog.h>

#include <charconv>
#include <chrono>
#include <sstream>
#include <string_view>
#include <vector>

namespace {
/// Map the process layer's WorkerState onto the tracker's neutral enum.
WorkerLiveness toLiveness(LlamaServerProcess::WorkerState state)
{
	switch (state) {
	case LlamaServerProcess::WorkerState::ALIVE:
		return WorkerLiveness::ALIVE;
	case LlamaServerProcess::WorkerState::DEAD:
		return WorkerLiveness::DEAD;
	case LlamaServerProcess::WorkerState::BUSY:
	default:
		return WorkerLiveness::BUSY;
	}
}
} // namespace

namespace {
constexpr auto POLL_INTERVAL = std::chrono::seconds(1);
constexpr auto HTTP_TIMEOUT_SECONDS = 5;

std::string getServerAddress()
{
	const auto server = ConfigManager::instance().getServerSettings();
	return "http://" + server.connectHost() + ":" + std::to_string(server.port);
}
} // namespace

ModelInfoMonitor::ModelInfoMonitor() : m_running(false)
{
}

ModelInfoMonitor::~ModelInfoMonitor()
{
	stop();
}

void ModelInfoMonitor::start()
{
	if (m_running.load(std::memory_order_acquire))
		return;

	m_running.store(true, std::memory_order_release);
	m_pollThread = std::thread(&ModelInfoMonitor::pollLoop, this);
}

void ModelInfoMonitor::stop()
{
	if (!m_running.load(std::memory_order_acquire))
		return;

	{
		// Lock so the store cannot slip between the poll thread's predicate
		// check and its wait — without it the notify could be lost and stop()
		// would block a full poll interval.
		std::lock_guard<std::mutex> lock(m_stopMutex);
		m_running.store(false, std::memory_order_release);
	}
	m_stopCv.notify_all();
	if (m_pollThread.joinable()) {
		m_pollThread.join();
	}
}

std::map<std::string, ModelInfo> ModelInfoMonitor::getAllStats() const
{
	// View over the tracker's snapshot: only models the server actually serves
	// (LOADED) appear as stats. LOADING/UNLOADING/CRASHED are lifecycle states
	// the panel reads from the tracker directly.
	std::map<std::string, ModelInfo> out;
	for (const auto &[id, state] : ModelStateTracker::instance().snapshot()) {
		if (state.lifecycle == ModelLifecycle::LOADED)
			out[id] = state.stats;
	}
	return out;
}

ModelInfo ModelInfoMonitor::getStatsFor(const std::string &name) const
{
	const auto all = getAllStats();
	auto it = all.find(name);
	return it == all.end() ? ModelInfo{} : it->second;
}

ModelInfo ModelInfoMonitor::getStats() const
{
	// Aggregate over the per-model view, mirroring the prior single-model shape.
	const auto all = getAllStats();
	ModelInfo aggregate;
	aggregate.isServerRunning = LlamaServerProcess::instance().isServerHealthy();
	aggregate.isModelLoaded = !all.empty();
	if (all.empty()) {
		aggregate.loadedModel =
			aggregate.isServerRunning ? "Model: None" : "Server: Offline";
		aggregate.isIdle = true;
		aggregate.activityState = ActivityState::IDLE;
		return aggregate;
	}
	aggregate.loadedModel = all.begin()->second.loadedModel;
	aggregate.isIdle = true;
	aggregate.activityState = ActivityState::IDLE;
	for (const auto &[id, m] : all) {
		aggregate.activeRequestCount += m.activeRequestCount;
		aggregate.isIdle = aggregate.isIdle && m.isIdle;
		if (m.activityState > aggregate.activityState)
			aggregate.activityState = m.activityState;
	}
	return aggregate;
}

bool ModelInfoMonitor::isProcessing(const std::string &slotJson)
{
	// Look for "is_processing":true in the slots response
	// Example:
	// [{"id":0,"n_ctx":9216,"speculative":false,"is_processing":false},...]
	return slotJson.find("\"is_processing\":true") != std::string::npos;
}

void ModelInfoMonitor::pollLoop()
{
	auto &tracker = ModelStateTracker::instance();

	while (m_running.load(std::memory_order_acquire)) {
		if (!LlamaServerProcess::instance().isServerHealthy()) {
			spdlog::debug("ModelInfoMonitor: server not healthy");
			tracker.onServerOffline();
			m_wasProcessing.clear();
			m_lastStats.clear();
		} else if (tracker.shouldSkipModelQueries()) {
			// Intentional unload-all: avoid /models and /slots so the router
			// does not auto-reload (issue #71).
			spdlog::debug(
				"ModelInfoMonitor: skipping model queries (unloaded all)");
			m_wasProcessing.clear();
			m_lastStats.clear();
		} else {
			// Gather the server's currently-loaded ids and feed raw truth into
			// the tracker. The probe/gatherStats callables do the per-model
			// HTTP; the tracker owns the lifecycle decisions (crash vs clean
			// unload).
			const auto ids = LlamaServerProcess::instance().getLoadedModelIds();

			auto probe = [](const std::string &id) {
				return toLiveness(
					LlamaServerProcess::instance().probeModelWorker(id));
			};
			// gatherStats MUST NOT call back into the tracker: it runs inside
			// ingestPoll's lockless gather phase, but re-entering any tracker
			// method risks the Phase-4a deadlock. "Previous stats" comes from
			// the poll-thread-private m_lastStats, not tracker.snapshot().
			auto gatherStats = [this](const std::string &id) {
				ModelInfo previous;
				auto it = m_lastStats.find(id);
				if (it != m_lastStats.end())
					previous = it->second;
				ModelInfo fresh = pollModel(id, previous);
				m_lastStats[id] = fresh;
				return fresh;
			};

			tracker.ingestPoll(ids, probe, gatherStats);

			// Drop transition state for ids no longer loaded.
			const auto loaded = tracker.snapshot();
			for (auto it = m_wasProcessing.begin();
				 it != m_wasProcessing.end();) {
				it = loaded.count(it->first) ? std::next(it)
											 : m_wasProcessing.erase(it);
			}
			for (auto it = m_lastStats.begin(); it != m_lastStats.end();) {
				it = loaded.count(it->first) ? std::next(it)
											 : m_lastStats.erase(it);
			}
		}

		// Interruptible wait: stop() notifies m_stopCv so shutdown does not
		// have to ride out the remainder of the poll interval.
		std::unique_lock<std::mutex> lock(m_stopMutex);
		m_stopCv.wait_for(lock, POLL_INTERVAL, [this] {
			return !m_running.load(std::memory_order_acquire);
		});
	}
}

ModelInfo ModelInfoMonitor::pollModel(const std::string &id,
									  const ModelInfo &previous)
{
	ModelInfo info;
	info.isServerRunning = true;
	info.isModelLoaded = true;
	info.loadedModel = id;

	const auto slotStatus = LlamaServerProcess::instance().getSlotStatus(id);
	const bool currentlyProcessing = isProcessing(slotStatus);

	// Count active requests - "is_processing":true occurrences.
	constexpr std::string_view kProcessingKey = "\"is_processing\":true";
	int activeCount = 0;
	size_t pos = 0;
	while ((pos = slotStatus.find(kProcessingKey, pos)) != std::string::npos) {
		activeCount++;
		pos += kProcessingKey.size();
	}
	info.activeRequestCount = activeCount;
	info.isIdle = !currentlyProcessing;

	// Finer activity: a processing slot that has decoded a token is generating;
	// one that hasn't is still prefilling the prompt.
	if (!currentlyProcessing) {
		info.activityState = ActivityState::IDLE;
	} else if (maxDecodedTokens(slotStatus) > 0) {
		info.activityState = ActivityState::GENERATING;
	} else {
		info.activityState = ActivityState::PROMPT;
	}

	// Detect transition: was processing, now idle -> fetch metrics once.
	bool &wasProcessing = m_wasProcessing[id];
	if (wasProcessing != currentlyProcessing) {
		spdlog::debug("ModelInfoMonitor: '{}' processing {} -> {}",
					  id,
					  wasProcessing,
					  currentlyProcessing);
	}
	if (wasProcessing && !currentlyProcessing) {
		spdlog::debug("ModelInfoMonitor: '{}' slot transitioned to idle, "
					  "fetching metrics",
					  id);
		auto metrics = fetchMetricsOnce(id);
		info.generationTokensPerSec = metrics.generationTokensPerSec;
		info.processingTokensPerSec = metrics.processingTokensPerSec;
		info.totalPromptTokens = metrics.totalPromptTokens;
		info.totalGenerationTokens = metrics.totalGenerationTokens;
	} else {
		// Carry the previous snapshot's counters forward while processing or
		// idle without a fresh fetch.
		info.generationTokensPerSec = previous.generationTokensPerSec;
		info.processingTokensPerSec = previous.processingTokensPerSec;
		info.totalPromptTokens = previous.totalPromptTokens;
		info.totalGenerationTokens = previous.totalGenerationTokens;
	}

	wasProcessing = currentlyProcessing;
	return info;
}

ModelInfo ModelInfoMonitor::fetchMetricsOnce(const std::string &modelName)
{
	ModelInfo info;
	info.generationTokensPerSec = 0.0;
	info.processingTokensPerSec = 0.0;
	info.totalPromptTokens = 0;
	info.totalGenerationTokens = 0;

	HttpClient client;
	client.setTimeout(HTTP_TIMEOUT_SECONDS);

	// Model ids can contain spaces; encode so the query value is well-formed.
	auto url = getServerAddress() +
			   "/metrics?model=" + HttpClient::urlEncode(modelName);
	auto [success, response] = client.get(url);

	if (!success) {
		spdlog::warn("ModelInfoMonitor: failed to fetch metrics: {}", response);
		return info;
	}

	return parseMetricsResponse(response);
}

ModelInfo ModelInfoMonitor::parseMetricsResponse(const std::string &response)
{
	ModelInfo info;
	info.generationTokensPerSec = 0.0;
	info.processingTokensPerSec = 0.0;
	info.totalPromptTokens = 0;
	info.totalGenerationTokens = 0;

	// Parse Prometheus-style metrics from llama-server.
	// llama-server returns metrics with a "llamacpp:" namespace prefix and
	// optional {label} suffixes, e.g.:
	//   llamacpp:predicted_tokens_seconds{model="..."} 67.8
	//   llamacpp:prompt_tokens_seconds{model="..."} 1664.8
	//   llamacpp:prompt_tokens_total{model="..."} 98986
	//   llamacpp:tokens_predicted_total{model="..."} 6829

	std::istringstream stream(response);
	std::string line;

	while (std::getline(stream, line)) {
		// Skip comments and empty lines
		if (line.empty() || line[0] == '#')
			continue;

		// Format: metric_name{labels} value  (labels are optional)
		size_t spacePos = line.find(' ');
		if (spacePos == std::string::npos)
			continue;

		// Strip any {label...} suffix to get the bare metric name
		std::string metricName = line.substr(0, spacePos);
		size_t bracePos = metricName.find('{');
		if (bracePos != std::string::npos)
			metricName = metricName.substr(0, bracePos);

		std::string valueStr = line.substr(spacePos + 1);

		try {
			double value = std::stod(valueStr);

			if (metricName == "llamacpp:predicted_tokens_seconds") {
				info.generationTokensPerSec = value;
			} else if (metricName == "llamacpp:prompt_tokens_seconds") {
				info.processingTokensPerSec = value;
			} else if (metricName == "llamacpp:prompt_tokens_total") {
				info.totalPromptTokens = static_cast<uint64_t>(value);
			} else if (metricName == "llamacpp:tokens_predicted_total") {
				info.totalGenerationTokens = static_cast<uint64_t>(value);
			}
		} catch (const std::exception &e) {
			spdlog::debug("ModelInfoMonitor: failed to parse metric line: {}",
						  line);
		}
	}

	return info;
}

std::string ModelInfoMonitor::parseLoadedModelId(const std::string &response)
{
	// Find a model with status "loaded" and extract its "id".
	auto loadedPos = response.find("\"loaded\"");
	if (loadedPos == std::string::npos)
		return "";
	// Search backwards from "loaded" to find the nearest "id" field.
	auto idPos = response.rfind("\"id\"", loadedPos);
	if (idPos == std::string::npos)
		return "";
	auto colonPos = response.find(":", idPos);
	if (colonPos == std::string::npos)
		return "";
	// Find start of value (skip whitespace and quotes).
	size_t pos = colonPos + 1;
	while (pos < response.size() &&
		   (response[pos] == ' ' || response[pos] == '"'))
		pos++;
	size_t end = pos;
	while (end < response.size() && response[end] != '"' && response[end] != ',')
		end++;
	if (end > pos) {
		return response.substr(pos, end - pos);
	}
	return "";
}

std::vector<std::string>
ModelInfoMonitor::parseLoadedModelIds(const std::string &response)
{
	std::vector<std::string> ids;

	if (response.empty())
		return ids;

	const auto doc =
		nlohmann::json::parse(response, nullptr, /*exceptions=*/false);
	if (doc.is_discarded())
		return ids;

	if (!doc.contains("data") || !doc["data"].is_array())
		return ids;

	for (const auto &entry : doc["data"]) {
		if (!entry.is_object())
			continue;
		if (!entry.contains("id") || !entry["id"].is_string())
			continue;
		if (!entry.contains("status") || !entry["status"].is_object())
			continue;

		const auto &status = entry["status"];
		if (!status.contains("value") || !status["value"].is_string())
			continue;

		if (status["value"].get<std::string>() == "loaded") {
			ids.push_back(entry["id"].get<std::string>());
		}
	}

	return ids;
}

bool ModelInfoMonitor::evaluateProbeResult(LlamaServerProcess::WorkerState state,
										   int &deadCount)
{
	// Single source of the gate: delegate to the tracker so semantics never
	// diverge. Retained as a static so the existing monitor unit tests still
	// exercise the contract through this entry point.
	return ModelStateTracker::evaluateProbe(toLiveness(state), deadCount);
}

int ModelInfoMonitor::maxDecodedTokens(const std::string &slotJson)
{
	int maxDecoded = 0;
	const std::string key = "\"n_decoded\":";
	size_t pos = 0;
	while ((pos = slotJson.find(key, pos)) != std::string::npos) {
		size_t valueStart = pos + key.size();
		// Skip any whitespace between the colon and the number.
		while (valueStart < slotJson.size() &&
			   (slotJson[valueStart] == ' ' || slotJson[valueStart] == '\t')) {
			valueStart++;
		}
		int value = 0;
		auto [ptr, ec] = std::from_chars(slotJson.data() + valueStart,
										 slotJson.data() + slotJson.size(),
										 value);
		if (ec == std::errc() && value > maxDecoded) {
			maxDecoded = value;
		}
		pos = valueStart;
	}
	return maxDecoded;
}