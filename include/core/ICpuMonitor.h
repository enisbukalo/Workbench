#pragma once

#include "processorStats.h"

/**
 * @file ICpuMonitor.h
 * @brief Thin interface for CPU monitoring.
 *
 * Panels depend on this interface rather than the CpuMonitor singleton,
 * enabling unit testing with GMock. The real CpuMonitor implements
 * this interface directly (zero indirection overhead).
 */
class ICpuMonitor
{
  public:
    virtual ~ICpuMonitor() = default;

    /** @brief Samples CPU usage and updates the internal snapshot. */
    virtual void update() = 0;

    /** @brief Returns the latest ProcessorStats snapshot. */
    virtual ProcessorStats getStats() const = 0;
};
