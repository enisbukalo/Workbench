#pragma once

#include "ICpuMonitor.h"
#include "processorStats.h"
#include "temperatureAverager.h"

#include <mutex>
#include <optional>

/**
 * @file cpuMonitor.h
 * @brief Thread-safe singleton that measures CPU utilisation.
 *
 * This class provides a singleton interface for monitoring CPU usage
 * across platforms. The implementation uses platform-specific mechanisms:
 * - Linux: Parses /proc/stat to calculate CPU idle time deltas
 * - Windows: Uses GetSystemTimes() API to measure idle time
 * - Unknown platform: Returns zero usage (no-op implementation)
 *
 * The monitor maintains thread-safe access to the latest CPU statistics
 * snapshot via mutex protection. All public methods are safe to call
 * from multiple threads concurrently.
 *
 * Sampling interval: 50-100ms depending on platform
 * Update frequency: Determined by the caller; typically 500ms via
 * SystemMonitorRunner
 */
class CpuMonitor : public ICpuMonitor
{
  public:
	/**
	 * @brief Returns the process-wide singleton instance.
	 *
	 * This implements the Meyers' singleton pattern using a function-local
	 * static variable, which guarantees thread-safe lazy initialization in
	 * C++11 and later. The instance is created on first call and persists
	 * for the lifetime of the program.
	 *
	 * @return Reference to the single @c CpuMonitor object.
	 * @note The instance is lazily initialized on first call.
	 */
	static CpuMonitor &instance()
	{
		static CpuMonitor monitor;
		return monitor;
	}

	/**
	 * @brief Samples CPU usage and updates the internal snapshot.
	 *
	 * This method triggers a CPU usage measurement by:
	 * 1. Recording the current CPU time (idle and total)
	 * 2. Waiting for a short sampling interval (50ms on Windows, 100ms on Linux)
	 * 3. Recording the CPU time again
	 * 4. Calculating the usage percentage from the time deltas
	 *
	 * The calculation formula is: usage = 1 - (idle_delta / total_delta)
	 *
	 * Platform implementations:
	 * - Linux: Reads /proc/stat twice with a delay between reads
	 * - Windows: Calls GetSystemTimes() twice with a delay between calls
	 * - Unknown: No-op; stats remain unchanged
	 *
	 * @note This method is thread-safe. Multiple concurrent calls will
	 *       serialize via the internal mutex.
	 */
	void update() override;

	/**
	 * @brief Returns the latest @c ProcessorStats snapshot under lock.
	 *
	 * This method retrieves the most recently sampled CPU usage statistics.
	 * The snapshot is protected by a mutex to ensure thread-safe access.
	 *
	 * @return A copy of the most recently sampled @c ProcessorStats.
	 *         The usagePercentage field contains the latest CPU usage
	 *         value (0.0 to 100.0), or 0.0 if no update has been performed.
	 *         The temperatureC field is best-effort: on Linux it is read from
	 *         hwmon/thermal sysfs when a CPU sensor is present and reported as a
	 *         rolling 60-second average to damp sensor noise; on Windows/unknown
	 *         platforms it stays at the -1.0 "unavailable" sentinel.
	 * @note This method is thread-safe and can be called concurrently
	 *       with update() or other get_* methods.
	 * @see tryUpdate()
	 */
	ProcessorStats getStats() const override;

	/**
	 * @brief Calls @ref update() and returns the resulting stats.
	 *
	 * This convenience method combines update() and getStats() into a
	 * single call. It is primarily useful for testing or when the caller
	 * needs to verify that an update was successful.
	 *
	 * @return The new @c ProcessorStats on success. This will always
	 *         contain a valid ProcessorStats structure, even if the
	 *         sampling interval was too short to detect any CPU activity.
	 * @note This method is thread-safe. The update and read operations
	 *       are performed atomically with respect to other threads.
	 * @see update(), getStats()
	 */
	std::optional<ProcessorStats> tryUpdate();

  private:
	/**
	 * @brief Private default constructor for singleton pattern.
	 *
	 * The CpuMonitor class can only be instantiated through the instance()
	 * static method, which uses Meyers' singleton pattern.
	 */
	CpuMonitor() = default;

	ProcessorStats stats_;
	mutable std::mutex statsMutex_;

	/**
	 * @brief Rolling 60-second average of the CPU package temperature.
	 *
	 * Fed one sample per updateLinux() call; the smoothed value is what
	 * stats_.temperatureC carries. Mutated only under statsMutex_.
	 */
	TemperatureAverager tempAverager_;

	/**
	 * @brief Linux-specific CPU sampling implementation.
	 *
	 * Parses /proc/stat to extract CPU time fields and calculates
	 * usage by comparing idle time deltas over a 100ms sampling interval.
	 */
	void updateLinux();

	/**
	 * @brief Windows-specific CPU sampling implementation.
	 *
	 * Uses GetSystemTimes() API to measure idle time deltas over a
	 * 50ms sampling interval.
	 */
	void updateWindows();

	/**
	 * @brief Fallback implementation for unknown platforms.
	 *
	 * This method does nothing; stats_ remains unchanged.
	 */
	void updateUnknown();
};
