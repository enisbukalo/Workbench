#pragma once

#include <chrono>
#include <cstddef>
#include <deque>

/**
 * @file temperatureAverager.h
 * @brief Time-windowed rolling average for temperature readings.
 *
 * Smooths the noisy, instantaneous temperature samples produced by the CPU and
 * GPU monitors into a rolling mean over a fixed wall-clock window (60 s by
 * default). Because the monitors poll at a caller-controlled and dynamically
 * changeable interval (see SystemMonitorRunner), the window is expressed in
 * time rather than in a fixed number of samples: the average always covers the
 * last @p window seconds regardless of how many samples landed in it.
 *
 * Sentinel handling mirrors ProcessorStats: a sample < 0 means "no reading".
 * Such samples are neither stored nor allowed to influence the average; while
 * the live reading is unavailable, add() returns the sentinel so the UI keeps
 * rendering "N/A" instead of a stale or poisoned value.
 *
 * @note This class is not thread-safe. Each instance is expected to be owned
 *       and mutated by a single monitor under that monitor's own lock.
 */
class TemperatureAverager
{
  public:
	using Clock = std::chrono::steady_clock;
	using TimePoint = Clock::time_point;

	/// Sentinel shared with ProcessorStats: any value < 0 means "unavailable".
	static constexpr double UNAVAILABLE = -1.0;

	/**
	 * @brief Construct an averager over a fixed wall-clock window.
	 * @param window Length of the averaging window. Defaults to 60 seconds.
	 */
	explicit TemperatureAverager(
		std::chrono::seconds window = std::chrono::seconds(60)) noexcept;

	/**
	 * @brief Record a new sample and return the current rolling average.
	 *
	 * Samples older than the window (relative to @p now) are evicted before the
	 * mean is computed.
	 *
	 * @param value Instantaneous reading in °C, or a value < 0 to signal that
	 *              no reading is currently available.
	 * @param now   Sampling timestamp. Defaults to Clock::now(); an explicit
	 *              value is accepted so tests can drive time deterministically.
	 * @return The mean of all valid samples within the window, or
	 *         @ref UNAVAILABLE when @p value is a sentinel or no valid samples
	 *         remain in the window.
	 */
	[[nodiscard]] double add(double value, TimePoint now = Clock::now());

	/**
	 * @brief Number of valid samples currently retained in the window.
	 * @note Primarily exposed for testing.
	 */
	[[nodiscard]] std::size_t sampleCount() const noexcept;

  private:
	struct Sample
	{
		TimePoint timestamp;
		double value;
	};

	/// Drop samples whose timestamp is older than @p now minus the window.
	void evictExpired(TimePoint now) noexcept;

	std::chrono::seconds m_window;
	std::deque<Sample> m_samples;
};
