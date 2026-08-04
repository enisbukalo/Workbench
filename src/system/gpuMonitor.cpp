/**
 * @file gpuMonitor.cpp
 * @brief NVIDIA GPU monitoring implementation.
 *
 * Parses nvidia-smi CSV output to extract VRAM usage, GPU utilization, and
 * temperature statistics for all detected NVIDIA GPUs. Provides thread-safe
 * accessors for both memory and load statistics.
 */

#include "gpuMonitor.h"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#define POPEN _popen
#define PCLOSE _pclose
#else
#define POPEN popen
#define PCLOSE pclose
#endif

void GpuMonitor::update()
{
	// Execute nvidia-smi with CSV output format for GPU statistics
	// Query: index, memory.total, memory.used, memory.free, utilization.gpu,
	//        temperature.gpu
	FILE *pipe = POPEN(
		"nvidia-smi "
		"--query-gpu=index,memory.total,memory.used,memory.free,utilization.gpu,"
		"temperature.gpu"
		" --format=csv,noheader,nounits",
		"r");
	if (!pipe) {
		spdlog::error("Failed to query GPU (nvidia-smi not found or error)");
		return;
	}

	std::vector<MemoryStats> newStats;
	std::vector<ProcessorStats> newLoadStats;
	// GPU index per load-stats entry, parallel to newLoadStats, so each raw
	// temperature can be routed to its device's rolling averager under the lock.
	std::vector<int> newLoadIndices;

	char buffer[256];
	// Parse CSV output line by line
	while (std::fgets(buffer, sizeof(buffer), pipe)) {
		int index, utilPct, tempC;
		uint64_t total, used, free;
		// Parse: index, total_mb, used_mb, free_mb, utilization_pct,
		// temperature_c. Temperature is optional: some virtual/datacenter GPUs
		// report "[N/A]" and the %d for tempC fails to parse. Accept the line
		// when the first five fields parse (== 5), and only adopt the
		// temperature when the sixth field is present (== 6); otherwise it
		// stays at the ProcessorStats sentinel (-1.0 = unavailable).
		int parsed =
			std::sscanf(buffer,
						"%d, %" SCNu64 ", %" SCNu64 ", %" SCNu64 ", %d, %d",
						&index,
						&total,
						&used,
						&free,
						&utilPct,
						&tempC);
		if (parsed >= 5) {
			MemoryStats s;
			s.id = index;
			s.totalMb = total;
			s.usedMb = used;
			s.availableMb = free;
			// Calculate memory usage percentage
			s.usagePercentage = (total > 0) ? (used * 100.0 / total) : 0.0;
			newStats.push_back(s);

			ProcessorStats p;
			// GPU utilization is already a percentage from nvidia-smi
			p.usagePercentage = static_cast<double>(utilPct);
			// Raw temperature only when the sixth field parsed successfully;
			// otherwise it keeps the -1.0 sentinel. Smoothing into the rolling
			// average happens below, under the lock, where the per-GPU
			// averagers live.
			if (parsed == 6) {
				p.temperatureC = static_cast<double>(tempC);
			}
			newLoadStats.push_back(p);
			newLoadIndices.push_back(index);
		}
	}
	PCLOSE(pipe);

	// Update internal state under lock
	std::lock_guard<std::mutex> lock(statsMutex_);

	// Smooth each GPU's temperature through its own rolling 60s averager. The
	// raw sentinel (-1.0) passes through unchanged so the UI still shows "N/A".
	std::map<int, TemperatureAverager> liveAveragers;
	for (std::size_t i = 0; i < newLoadStats.size(); ++i) {
		const int index = newLoadIndices[i];
		// Move any existing averager so smoothing history survives this tick;
		// drop averagers for GPUs that no longer appear by not carrying them
		// over to liveAveragers.
		auto [it, inserted] = liveAveragers.try_emplace(index);
		if (auto old = tempAveragers_.find(index); old != tempAveragers_.end()) {
			it->second = std::move(old->second);
		}
		newLoadStats[i].temperatureC =
			it->second.add(newLoadStats[i].temperatureC);
	}
	tempAveragers_ = std::move(liveAveragers);

	stats_ = std::move(newStats);
	loadStats_ = std::move(newLoadStats);
}

std::vector<MemoryStats> GpuMonitor::getStats() const
{
	std::lock_guard<std::mutex> lock(statsMutex_);
	return stats_;
}

std::vector<ProcessorStats> GpuMonitor::getLoadStats() const
{
	std::lock_guard<std::mutex> lock(statsMutex_);
	return loadStats_;
}
