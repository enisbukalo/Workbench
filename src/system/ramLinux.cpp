/**
 * @file ramLinux.cpp
 * @brief Linux-specific RAM monitoring implementation.
 *
 * Parses /proc/meminfo to extract memory statistics and converts
 * kilobytes to mebibytes for display.
 */

#include "ramMonitor.h"
#include <cmath>
#include <fstream>
#include <sstream>

void MemoryMonitor::updateLinux()
{
	std::ifstream file("/proc/meminfo");

	uint64_t total = 0, available = 0;

	std::string line;
	// Parse /proc/meminfo line by line
	while (std::getline(file, line)) {
		std::istringstream iss(line);
		std::string key, value, unit;
		iss >> key >> value >> unit;

		// Extract MemTotal and MemAvailable fields (values in kB)
		if (key == "MemTotal:") {
			// Convert kB to MiB
			total = std::round(std::stoull(value) / 1024.0);
		} else if (key == "MemAvailable:") {
			// Convert kB to MiB
			available = std::round(std::stoull(value) / 1024.0);
		}
	}

	MemoryStats newStats;
	newStats.totalMb = total;
	newStats.availableMb = available;
	newStats.usedMb = total - available;
	// Calculate usage percentage
	newStats.usagePercentage =
		total > 0 ? (newStats.usedMb * 100.0 / total) : 0.0;

	std::lock_guard<std::mutex> lock(statsMutex_);
	stats_ = newStats;
}

void MemoryMonitor::update()
{
	updateLinux();
}
