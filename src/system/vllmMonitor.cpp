/**
 * @file vllmMonitor.cpp
 * @brief Implementation of VllmMonitor singleton.
 *
 * Polls vLLM server endpoints at 1Hz for model metrics.
 * Thread-safe via mutex-protected access.
 */

#include "vllmMonitor.h"
#include "configManager.h"
#include "httpClient.h"
#include "json.hpp"

#include <spdlog/spdlog.h>

#include <chrono>
#include <sstream>
#include <string_view>

namespace {
constexpr auto POLL_INTERVAL = std::chrono::seconds(1);
constexpr auto HTTP_TIMEOUT_SECONDS = 5;
} // namespace

VllmMonitor::VllmMonitor() : m_running(false)
{
}

void VllmThroughputTracker::reset()
{
	m_initialized = false;
	m_wasActive = false;
	m_generationStarted = false;
	m_lastPromptTokens = 0;
	m_lastGenerationTokens = 0;
	m_lastSampleAt = Clock::time_point{};
	m_promptTokens = 0.0;
	m_promptSeconds = 0.0;
	m_generationTokens = 0.0;
	m_generationSeconds = 0.0;
	m_processingAverage = 0.0;
	m_generationAverage = 0.0;
}

void VllmThroughputTracker::beginActivity()
{
	// A new request begins in prefill, but it contributes to the same retained
	// server-session averages. Only phase detection resets here.
	m_generationStarted = false;
}

void VllmThroughputTracker::accumulate(uint64_t promptDelta,
									   uint64_t generationDelta,
									   std::chrono::duration<double> elapsed)
{
	const double seconds = elapsed.count();
	if (seconds <= 0.0)
		return;

	if (promptDelta > 0) {
		m_promptTokens += static_cast<double>(promptDelta);
		m_promptSeconds += seconds;
		m_processingAverage = m_promptTokens / m_promptSeconds;
	}

	if (generationDelta > 0)
		m_generationStarted = true;
	if (m_generationStarted) {
		m_generationTokens += static_cast<double>(generationDelta);
		m_generationSeconds += seconds;
		m_generationAverage = m_generationTokens / m_generationSeconds;
	}
}

ModelInfo VllmThroughputTracker::update(const ModelInfo &counters,
										Clock::time_point sampledAt)
{
	ModelInfo result = counters;
	const bool active = counters.activeRequestCount > 0;
	result.isIdle = !active;

	const bool countersRolledBack =
		m_initialized &&
		(counters.totalPromptTokens < m_lastPromptTokens ||
		 counters.totalGenerationTokens < m_lastGenerationTokens);
	if (!m_initialized || countersRolledBack) {
		reset();
		m_initialized = true;
		m_wasActive = active;
		m_lastPromptTokens = counters.totalPromptTokens;
		m_lastGenerationTokens = counters.totalGenerationTokens;
		m_lastSampleAt = sampledAt;
		if (active)
			beginActivity();
		result.processingTokensPerSec = 0.0;
		result.generationTokensPerSec = 0.0;
		result.activityState =
			active ? ActivityState::PROMPT : ActivityState::IDLE;
		return result;
	}

	if (active && !m_wasActive)
		beginActivity();

	const auto promptDelta = counters.totalPromptTokens - m_lastPromptTokens;
	const auto generationDelta =
		counters.totalGenerationTokens - m_lastGenerationTokens;
	if (active || m_wasActive) {
		accumulate(promptDelta, generationDelta, sampledAt - m_lastSampleAt);
	}

	result.processingTokensPerSec = m_processingAverage;
	result.generationTokensPerSec = m_generationAverage;
	result.activityState = active
							   ? (m_generationStarted ? ActivityState::GENERATING
													  : ActivityState::PROMPT)
							   : ActivityState::IDLE;

	m_wasActive = active;
	m_lastPromptTokens = counters.totalPromptTokens;
	m_lastGenerationTokens = counters.totalGenerationTokens;
	m_lastSampleAt = sampledAt;
	return result;
}

VllmMonitor::~VllmMonitor()
{
	stop();
}

void VllmMonitor::start()
{
	if (m_running.load(std::memory_order_acquire))
		return;

	m_running.store(true, std::memory_order_release);
	m_pollThread = std::thread(&VllmMonitor::pollLoop, this);
}

void VllmMonitor::stop()
{
	if (!m_running.load(std::memory_order_acquire))
		return;

	{
		std::lock_guard<std::mutex> lock(m_stopMutex);
		m_running.store(false, std::memory_order_release);
	}
	m_stopCv.notify_all();
	if (m_pollThread.joinable()) {
		m_pollThread.join();
	}
}

ModelInfo VllmMonitor::getStats() const
{
	std::lock_guard<std::mutex> lock(m_statsMutex);
	return m_stats;
}

std::string VllmMonitor::buildUrl(const std::string &host, int port)
{
	return "http://" + host + ":" + std::to_string(port);
}

void VllmMonitor::pollLoop()
{
	while (m_running.load(std::memory_order_acquire)) {
		const auto cfg = ConfigManager::instance().getConfigSnapshot().vllm;

		if (cfg.host.empty()) {
			// vLLM disabled: clear stats and sleep
			{
				std::lock_guard<std::mutex> lock(m_statsMutex);
				m_stats = ModelInfo{};
				m_stats.isServerRunning = false;
			}
			m_lastHost.clear();
			m_lastPort = 0;
		} else {
			pollOnce(cfg.host, cfg.port);
		}

		std::unique_lock<std::mutex> lock(m_stopMutex);
		m_stopCv.wait_for(lock, POLL_INTERVAL, [this] {
			return !m_running.load(std::memory_order_acquire);
		});
	}
}

void VllmMonitor::pollOnce(const std::string &host, int port)
{
	// Reset delta state on host/port change
	const bool configChanged = host != m_lastHost || port != m_lastPort;
	if (configChanged) {
		m_throughputTracker.reset();
		m_lastHost = host;
		m_lastPort = port;
	}

	HttpClient client;
	client.setTimeout(HTTP_TIMEOUT_SECONDS);

	const std::string baseUrl = buildUrl(host, port);

	// 1. /health — determine if server is running
	auto [healthResult, healthBody] = client.getWithResult(baseUrl + "/health");
	const bool isServerRunning = healthResult == HttpClient::HttpResult::OK;

	if (!isServerRunning) {
		spdlog::debug("VllmMonitor: server not reachable ({})", host);
		std::lock_guard<std::mutex> lock(m_statsMutex);
		m_stats = ModelInfo{};
		m_stats.isServerRunning = false;
		return;
	}

	// 2. /v1/models — discover loaded model
	auto [modelsResult, modelsBody] =
		client.getWithResult(baseUrl + "/v1/models");
	std::string loadedModel;
	bool isModelLoaded = false;
	if (modelsResult == HttpClient::HttpResult::OK) {
		loadedModel = parseModelsResponse(modelsBody);
		isModelLoaded = !loadedModel.empty();
	}

	// 3. /metrics — parse counters and gauges
	auto [metricsResult, metricsBody] =
		client.getWithResult(baseUrl + "/metrics");
	ModelInfo parsed;
	if (metricsResult == HttpClient::HttpResult::OK) {
		parsed = parseMetricsResponse(metricsBody);
	}

	ModelInfo throughput = parsed;
	if (metricsResult == HttpClient::HttpResult::OK) {
		throughput =
			m_throughputTracker.update(parsed,
									   VllmThroughputTracker::Clock::now());
	}

	// Build final snapshot
	ModelInfo info;
	info.isServerRunning = true;
	info.isModelLoaded = isModelLoaded;
	info.loadedModel = loadedModel;
	info.generationTokensPerSec = throughput.generationTokensPerSec;
	info.processingTokensPerSec = throughput.processingTokensPerSec;
	info.totalPromptTokens = parsed.totalPromptTokens;
	info.totalGenerationTokens = parsed.totalGenerationTokens;
	info.activeRequestCount = parsed.activeRequestCount;
	info.isIdle = throughput.isIdle;
	info.activityState = throughput.activityState;

	{
		std::lock_guard<std::mutex> lock(m_statsMutex);
		m_stats = info;
	}
}

std::string VllmMonitor::parseModelsResponse(const std::string &response)
{
	// vLLM /v1/models returns OpenAI-compatible JSON:
	// {"object":"list","data":[{"id":"model-name","object":"model",...}]}
	if (response.empty())
		return "";

	const auto doc =
		nlohmann::json::parse(response, nullptr, /*exceptions=*/false);
	if (doc.is_discarded())
		return "";

	if (!doc.contains("data") || !doc["data"].is_array())
		return "";

	for (const auto &entry : doc["data"]) {
		if (!entry.is_object())
			continue;
		if (!entry.contains("id") || !entry["id"].is_string())
			continue;
		return entry["id"].get<std::string>();
	}

	return "";
}

ModelInfo VllmMonitor::parseMetricsResponse(const std::string &response)
{
	ModelInfo info;
	info.generationTokensPerSec = 0.0;
	info.processingTokensPerSec = 0.0;
	info.totalPromptTokens = 0;
	info.totalGenerationTokens = 0;
	info.activeRequestCount = 0;

	// Parse Prometheus-style metrics from vLLM.
	// vLLM returns metrics with a "vllm:" namespace prefix and
	// optional {label} suffixes, e.g.:
	//   vllm:prompt_tokens_total{model_name="..."} 98986
	//   vllm:generation_tokens_total{model_name="..."} 6829
	//   vllm:num_requests_running{model_name="..."} 2
	//   vllm:num_requests_waiting{model_name="..."} 0

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

			if (metricName == "vllm:prompt_tokens_total") {
				info.totalPromptTokens = static_cast<uint64_t>(value);
			} else if (metricName == "vllm:generation_tokens_total") {
				info.totalGenerationTokens = static_cast<uint64_t>(value);
			} else if (metricName == "vllm:num_requests_running") {
				info.activeRequestCount = static_cast<int>(value);
			}
			// num_requests_waiting is used for activityState derivation
			// in pollOnce (not stored in ModelInfo directly).
		} catch (const std::exception &e) {
			spdlog::debug("VllmMonitor: failed to parse metric line: {}", line);
		}
	}

	return info;
}
