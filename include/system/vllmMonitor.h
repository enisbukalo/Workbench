#pragma once

#include "IVllmMonitor.h"
#include "modelInfo.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

/**
 * @brief Converts cumulative vLLM counters into retained server-session
 * averages.
 *
 * The first sample establishes a baseline. Active windows contribute token
 * deltas and measured monotonic poll intervals to server-session running
 * averages. The averages survive idle periods and later requests. Counter
 * rollback starts a fresh baseline without emitting a spike.
 */
class VllmThroughputTracker
{
  public:
	using Clock = std::chrono::steady_clock;

	/** Reset all baselines and retained averages. */
	void reset();

	/**
	 * @brief Fold one parsed metrics sample into the server-session averages.
	 * @param counters Parsed cumulative counters and active request count.
	 * @param sampledAt Monotonic time at which the sample was collected.
	 * @return The sample with rates and activity state derived from the window.
	 */
	[[nodiscard]] ModelInfo update(const ModelInfo &counters,
								   Clock::time_point sampledAt);

  private:
	void beginActivity();
	void accumulate(uint64_t promptDelta,
					uint64_t generationDelta,
					std::chrono::duration<double> elapsed);

	bool m_initialized = false;
	bool m_wasActive = false;
	bool m_generationStarted = false;
	uint64_t m_lastPromptTokens = 0;
	uint64_t m_lastGenerationTokens = 0;
	Clock::time_point m_lastSampleAt{};
	double m_promptTokens = 0.0;
	double m_promptSeconds = 0.0;
	double m_generationTokens = 0.0;
	double m_generationSeconds = 0.0;
	double m_processingAverage = 0.0;
	double m_generationAverage = 0.0;
};

/**
 * @file vllmMonitor.h
 * @brief Thread that polls an external vLLM server for model metrics.
 *
 * Polls /health, /v1/models, and /metrics at 1Hz. The metrics are parsed
 * from Prometheus-format counters and gauges. Token throughput is derived
 * from cumulative counter deltas over active request windows. The combined
 * averages remain visible while idle and continue across later requests.
 *
 * Thread-safe: all public methods can be called concurrently.
 *
 * Lifecycle mirrors ModelInfoMonitor: start()/stop() with atomic m_running
 * + condition variable for clean shutdown.
 *
 * Config-aware: reads VllmSettings from ConfigManager each cycle. Empty host
 * disables polling; host/port change resets delta state.
 */
class VllmMonitor : public IVllmMonitor
{
  public:
	/**
	 * @brief Returns the process-wide singleton instance.
	 * @return Reference to the single @c VllmMonitor object.
	 */
	static VllmMonitor &instance()
	{
		static VllmMonitor monitor;
		return monitor;
	}

	/** @brief Starts the background polling thread. */
	void start();

	/** @brief Stops the background polling thread. */
	void stop();

	/**
	 * @brief Returns the latest ModelInfo snapshot from the vLLM server.
	 *
	 * Thread-safe: returns a copy taken under a mutex.
	 */
	ModelInfo getStats() const override;

	/**
	 * @brief Parses a vLLM /v1/models response body and returns the first
	 * loaded model id.
	 *
	 * @param response Raw JSON response body from /v1/models.
	 * @return First model id, or "" if none loaded or parse fails.
	 */
	[[nodiscard]] static std::string
	parseModelsResponse(const std::string &response);

	/**
	 * @brief Parses Prometheus-style metrics from vLLM /metrics endpoint.
	 *
	 * Extracts counters and gauges:
	 *   vllm:prompt_tokens_total      -> totalPromptTokens
	 *   vllm:generation_tokens_total  -> totalGenerationTokens
	 *   vllm:num_requests_running     -> activeRequestCount
	 *   vllm:num_requests_waiting     -> used for activityState derivation
	 *
	 * Label dimensions ({model_name="..."}) are stripped before parsing.
	 *
	 * @param response Raw response body from /metrics.
	 * @return ModelInfo with parsed cumulative counters.
	 */
	[[nodiscard]] static ModelInfo
	parseMetricsResponse(const std::string &response);

  private:
	VllmMonitor();
	~VllmMonitor();

	VllmMonitor(const VllmMonitor &) = delete;
	VllmMonitor &operator=(const VllmMonitor &) = delete;

	/** @brief Background polling loop. */
	void pollLoop();

	/**
	 * @brief Fetches /health, /v1/models, and /metrics in one cycle.
	 *
	 * Updates retained running averages from cumulative counters. Resets the
	 * activity baseline when host/port changes.
	 *
	 * @param host Current vllm host from config.
	 * @param port Current vllm port from config.
	 */
	void pollOnce(const std::string &host, int port);

	/** @brief Build the base URL from host:port. */
	static std::string buildUrl(const std::string &host, int port);

	// Thread control.
	std::atomic<bool> m_running;
	std::thread m_pollThread;
	std::condition_variable m_stopCv;
	std::mutex m_stopMutex;

	// Latest stats, protected by m_statsMutex.
	mutable std::mutex m_statsMutex;
	ModelInfo m_stats;

	// Delta computation state (poll-thread private).
	std::string m_lastHost;
	int m_lastPort = 0;
	VllmThroughputTracker m_throughputTracker;
};
