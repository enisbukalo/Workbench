#pragma once
#include <cstdint>

/**
 * @file processorStats.h
 * @brief Snapshot of processor utilisation for a single CPU or GPU compute
 * engine.
 *
 * This structure captures a point-in-time snapshot of processor load
 * statistics. The usage percentage represents the fraction of time the
 * processor spent executing non-idle tasks during the sampling interval.
 *
 * @note For CPU monitoring, this is calculated by comparing idle time
 *       deltas over a sampling period. For GPU monitoring, this value
 *       is obtained directly from nvidia-smi.
 *
 * @note The temperature field is only populated for GPUs (via nvidia-smi).
 *       It defaults to the sentinel -1.0 ("unavailable"), which is also
 *       what CPU snapshots and GPUs that report `[N/A]` will carry.
 *       Consumers should treat any value < 0 as "no reading".
 */
struct ProcessorStats
{
	double usagePercentage =
		0.0; ///< Processor load as a percentage of total capacity (0–100 %).
	double temperatureC = -1.0; ///< Temperature in °C; -1.0 means unavailable.
};
