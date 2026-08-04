/**
 * @file ramMonitor.cpp
 * @brief RAM monitor base implementation.
 *
 * Provides the platform-dispatched update() method and implements
 * common accessor methods for memory statistics. Platform-specific
 * implementations are in ramLinux.cpp and ramWindows.cpp.
 */

#include "ramMonitor.h"

MemoryStats MemoryMonitor::getStats() const
{
	std::lock_guard<std::mutex> lock(statsMutex_);
	return stats_;
}

std::optional<MemoryStats> MemoryMonitor::tryUpdate()
{
	update();
	return getStats();
}
