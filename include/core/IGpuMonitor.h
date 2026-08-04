#pragma once

#include "memoryStats.h"
#include "processorStats.h"

#include <vector>

/**
 * @file IGpuMonitor.h
 * @brief Thin interface for NVIDIA GPU monitoring.
 *
 * Panels depend on this interface rather than the GpuMonitor singleton,
 * enabling unit testing with GMock. The real GpuMonitor implements
 * this interface directly (zero indirection overhead).
 */
class IGpuMonitor
{
  public:
    virtual ~IGpuMonitor() = default;

    /** @brief Queries all GPUs and updates internal VRAM and load snapshots. */
    virtual void update() = 0;

    /** @brief Returns per-GPU VRAM usage snapshots. */
    virtual std::vector<MemoryStats> getStats() const = 0;

    /** @brief Returns per-GPU compute utilisation snapshots. */
    virtual std::vector<ProcessorStats> getLoadStats() const = 0;
};
