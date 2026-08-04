#pragma once

#include "IMemoryMonitor.h"
#include "memoryStats.h"

#include <mutex>
#include <optional>

/**
 * @file ramMonitor.h
 * @brief Thread-safe singleton that measures system RAM utilisation.
 *
 * This class provides a singleton interface for monitoring system memory
 * usage across platforms. The implementation uses platform-specific mechanisms:
 * - Linux: Parses /proc/meminfo to extract MemTotal and MemAvailable
 * - Windows: Uses GlobalMemoryStatusEx() API
 * - Unknown platform: Returns zero usage (no-op implementation)
 *
 * All capacity values are expressed in mebibytes (MiB) for consistency.
 * The monitor maintains thread-safe access to the latest memory statistics
 * snapshot via mutex protection. All public methods are safe to call
 * from multiple threads concurrently.
 *
 * Sampling interval: Determined by caller; typically 500ms via
 * SystemMonitorRunner
 */
class MemoryMonitor : public IMemoryMonitor
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
	 * @return Reference to the single @c MemoryMonitor object.
	 * @note The instance is lazily initialized on first call.
	 */
	static MemoryMonitor &instance()
	{
		static MemoryMonitor monitor;
		return monitor;
	}

	/**
	 * @brief Samples system RAM usage and updates the internal snapshot.
	 *
	 * This method triggers a memory usage measurement using platform-specific
	 * mechanisms:
	 * - Linux: Reads /proc/meminfo to extract MemTotal and MemAvailable fields
	 * - Windows: Calls GlobalMemoryStatusEx() API
	 * - Unknown: No-op; stats_ remains unchanged
	 *
	 * The calculated statistics include:
	 * - totalMb: Total system memory in MiB
	 * - usedMb: Currently used memory in MiB (total - available)
	 * - availableMb: Currently free memory in MiB
	 * - usagePercentage: Used memory as a percentage of total (0-100%)
	 *
	 * @note This method is thread-safe. Multiple concurrent calls will
	 *       serialize via the internal mutex.
	 * @note The stats_ vector is updated atomically; readers will never
	 *       see a partially-updated state.
	 */
	void update() override;

	/**
	 * @brief Returns the latest @c MemoryStats snapshot under lock.
	 *
	 * This method retrieves the most recently sampled system memory statistics.
	 * The snapshot is protected by a mutex to ensure thread-safe access.
	 *
	 * @return A copy of the most recently sampled @c MemoryStats containing:
	 *         - totalMb: Total system memory in MiB
	 *         - usedMb: Currently used memory in MiB
	 *         - availableMb: Currently free memory in MiB
	 *         - usagePercentage: Used memory as a percentage of total (0-100%)
	 *
	 * @note This method is thread-safe and can be called concurrently
	 *       with update() or other get_* methods.
	 * @see tryUpdate()
	 */
	MemoryStats getStats() const override;

	/**
	 * @brief Calls @ref update() and returns the resulting stats.
	 *
	 * This convenience method combines update() and getStats() into a
	 * single call. It is primarily useful for testing or when the caller
	 * needs to verify that an update was successful.
	 *
	 * @return The new @c MemoryStats on success. This will always
	 *         contain a valid MemoryStats structure, even if the
	 *         sampling interval was too short to detect any memory
	 *         activity.
	 * @note This method is thread-safe. The update and read operations
	 *       are performed atomically with respect to other threads.
	 * @see update(), getStats()
	 */
	std::optional<MemoryStats> tryUpdate();

  private:
	/**
	 * @brief Private default constructor for singleton pattern.
	 *
	 * The MemoryMonitor class can only be instantiated through the instance()
	 * static method, which uses Meyers' singleton pattern.
	 */
	MemoryMonitor() = default;

	MemoryStats stats_;
	mutable std::mutex statsMutex_;

	/**
	 * @brief Linux-specific memory sampling implementation.
	 *
	 * Parses /proc/meminfo to extract MemTotal and MemAvailable fields,
	 * converting from kilobytes to mebibytes.
	 */
	void updateLinux();

	/**
	 * @brief Windows-specific memory sampling implementation.
	 *
	 * Uses GlobalMemoryStatusEx() API to query system memory statistics,
	 * converting from bytes to mebibytes.
	 */
	void updateWindows();

	/**
	 * @brief Fallback implementation for unknown platforms.
	 *
	 * This method does nothing; stats_ remains unchanged.
	 */
	void updateUnknown();
};
