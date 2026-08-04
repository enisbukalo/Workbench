#pragma once

#include "IGpuMonitor.h"
#include "memoryStats.h"
#include "processorStats.h"
#include "temperatureAverager.h"

#include <map>
#include <mutex>
#include <vector>

/**
 * @file gpuMonitor.h
 * @brief Thread-safe singleton that queries NVIDIA GPU statistics via @c
 * nvidia-smi.
 *
 * This class provides a singleton interface for monitoring NVIDIA GPU
 * resources using the nvidia-smi command-line utility. It tracks two
 * categories of statistics:
 *
 * - Memory usage (VRAM): Total, used, available, and usage percentage
 * - Compute utilization: GPU core utilization percentage
 *
 * Platform support:
 * - Linux/Windows: Parses nvidia-smi CSV output
 * - Other platforms: No-op implementation; returns empty vectors
 *
 * All public methods are thread-safe. The mutex protects both the VRAM
 * and compute utilization vectors during updates and reads.
 *
 * Sampling interval: Determined by caller; typically 500ms via
 * SystemMonitorRunner
 */
class GpuMonitor : public IGpuMonitor
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
	 * @return Reference to the single @c GpuMonitor object.
	 * @note The instance is lazily initialized on first call.
	 */
	static GpuMonitor &instance()
	{
		static GpuMonitor monitor;
		return monitor;
	}

	/**
	 * @brief Queries all GPUs and updates the internal VRAM and load snapshots.
	 *
	 * This method executes nvidia-smi with the following query:
	 * `--query-gpu=index,memory.total,memory.used,memory.free,utilization.gpu,temperature.gpu`
	 *
	 * The CSV output is parsed line-by-line, with each line representing
	 * one GPU. For each GPU, both memory stats (VRAM) and compute load
	 * stats are updated simultaneously.
	 *
	 * Error handling:
	 * - If nvidia-smi is unavailable (not in PATH or not executable),
	 *   the method returns immediately without modifying internal state.
	 * - If the command returns non-zero exit code, the output is still
	 *   parsed if available, but errors are silently ignored.
	 *
	 * @note This method is thread-safe. Multiple concurrent calls will
	 *       serialize via the internal mutex.
	 * @note The stats_ and loadStats_ vectors are updated atomically;
	 *       readers will never see a partially-updated state.
	 */
	void update() override;

	/**
	 * @brief Returns per-GPU VRAM usage snapshots.
	 *
	 * This method retrieves the most recently sampled VRAM statistics for
	 * all detected NVIDIA GPUs. The vector is ordered by GPU device index,
	 * matching the order returned by nvidia-smi.
	 *
	 * @return A vector of @c MemoryStats, one entry per detected GPU, ordered by
	 *         device index. Each entry contains:
	 *         - id: GPU device index
	 *         - totalMb: Total VRAM capacity in MiB
	 *         - usedMb: Currently used VRAM in MiB
	 *         - availableMb: Free VRAM in MiB
	 *         - usagePercentage: VRAM usage as a percentage (0-100%)
	 *
	 * Returns an empty vector if:
	 * - No NVIDIA GPUs were detected
	 * - nvidia-smi is unavailable
	 * - update() has not been called yet
	 *
	 * @note This method is thread-safe and can be called concurrently
	 *       with update() or other get_* methods.
	 * @see getLoadStats()
	 */
	std::vector<MemoryStats> getStats() const override;

	/**
	 * @brief Returns per-GPU compute utilisation snapshots.
	 *
	 * This method retrieves the most recently sampled compute utilization
	 * statistics for all detected NVIDIA GPUs. The vector is ordered by
	 * GPU device index, matching the order returned by nvidia-smi.
	 *
	 * @return A vector of @c ProcessorStats, one entry per detected GPU, ordered
	 *         by device index. Each entry contains:
	 *         - usagePercentage: GPU compute utilization as a percentage
	 * (0-100%)
	 *         - temperatureC: GPU temperature in °C as a rolling 60-second
	 *           average, or -1.0 if the GPU reported "[N/A]" (e.g. some
	 *           virtual/datacenter GPUs)
	 *
	 * Returns an empty vector if:
	 * - No NVIDIA GPUs were detected
	 * - nvidia-smi is unavailable
	 * - update() has not been called yet
	 *
	 * @note This method is thread-safe and can be called concurrently
	 *       with update() or other get_* methods.
	 * @see getStats()
	 */
	std::vector<ProcessorStats> getLoadStats() const override;

  private:
	GpuMonitor() = default;
	std::vector<MemoryStats> stats_;
	std::vector<ProcessorStats> loadStats_;
	mutable std::mutex statsMutex_;

	/**
	 * @brief Per-GPU rolling 60-second temperature averages, keyed by GPU index.
	 *
	 * One averager per device persists across update() calls (the loadStats_
	 * vector is rebuilt each tick, so the smoothing state cannot live there).
	 * Mutated only under statsMutex_.
	 */
	std::map<int, TemperatureAverager> tempAveragers_;
};
