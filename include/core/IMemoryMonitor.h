#pragma once

#include "memoryStats.h"

/**
 * @file IMemoryMonitor.h
 * @brief Thin interface for system RAM monitoring.
 *
 * Panels depend on this interface rather than the MemoryMonitor singleton,
 * enabling unit testing with GMock. The real MemoryMonitor implements
 * this interface directly (zero indirection overhead).
 */
class IMemoryMonitor
{
  public:
    virtual ~IMemoryMonitor() = default;

    /** @brief Samples system RAM usage and updates the internal snapshot. */
    virtual void update() = 0;

    /** @brief Returns the latest MemoryStats snapshot. */
    virtual MemoryStats getStats() const = 0;
};
