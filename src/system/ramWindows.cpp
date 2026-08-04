/**
 * @file ramWindows.cpp
 * @brief Windows-specific RAM monitoring implementation.
 *
 * Uses Windows API GlobalMemoryStatusEx() to query system memory
 * statistics and converts bytes to mebibytes for display.
 */

#include "ramMonitor.h"
#include <cmath>
#include <windows.h>

void MemoryMonitor::updateWindows()
{
	MEMORYSTATUSEX statex{};
	statex.dwLength = sizeof(statex);

	// Query Windows API for memory statistics (values in bytes)
	if (GlobalMemoryStatusEx(&statex)) {
		MemoryStats newStats;
		// Convert bytes to MiB (1 MiB = 1048576 bytes)
		newStats.totalMb = std::round(statex.ullTotalPhys / 1048576.0);
		newStats.usedMb =
			std::round((statex.ullTotalPhys - statex.ullAvailPhys) / 1048576.0);
		newStats.availableMb = std::round(statex.ullAvailPhys / 1048576.0);
		// Calculate usage percentage
		newStats.usagePercentage =
			(newStats.totalMb > 0) ? (newStats.usedMb * 100.0 / newStats.totalMb)
								   : 0.0;

		std::lock_guard<std::mutex> lock(statsMutex_);
		stats_ = newStats;
	}
}

void MemoryMonitor::update()
{
	updateWindows();
}
