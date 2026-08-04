/**
 * @file cpuMonitor.cpp
 * @brief CPU monitor base implementation.
 *
 * Provides the platform-dispatched update() method and implements
 * common accessor methods for CPU statistics. Platform-specific
 * implementations are in cpuLinux.cpp and cpuWindows.cpp.
 */

#include "cpuMonitor.h"

ProcessorStats CpuMonitor::getStats() const
{
	std::lock_guard<std::mutex> lock(statsMutex_);
	return stats_;
}

std::optional<ProcessorStats> CpuMonitor::tryUpdate()
{
	update();
	return getStats();
}
