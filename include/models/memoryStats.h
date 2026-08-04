#pragma once
#include <cstdint>

/**
 * @file memoryStats.h
 * @brief Snapshot of memory usage for a single hardware device (RAM or GPU
 * VRAM).
 *
 * This structure captures a point-in-time snapshot of memory statistics
 * for any memory-capable hardware device. All capacity values are expressed
 * in mebibytes (MiB) for consistency across platforms.
 *
 * @note The `id` field is used for GPU devices to identify the specific
 *       GPU index. For system RAM, this value is typically 0.
 */
struct MemoryStats
{
	int id = 0;			  ///< Zero-based device index (GPU index or 0 for RAM).
	uint64_t totalMb = 0; ///< Total memory capacity in MiB.
	uint64_t usedMb = 0;  ///< Currently used memory in MiB.
	uint64_t availableMb = 0; ///< Currently free memory in MiB.
	double usagePercentage =
		0.0; ///< Used memory as a percentage of total (0–100 %).
};
