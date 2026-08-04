/**
 * @file cpuWindows.cpp
 * @brief Windows-specific CPU monitoring implementation.
 *
 * Uses Windows API GetSystemTimes() to measure CPU utilization by
 * comparing idle time deltas over a 50ms sampling interval.
 */

#include "cpuMonitor.h"
#include <thread>
#include <windows.h>

void CpuMonitor::updateWindows()
{
	// Take first snapshot of system times
	FILETIME prevIdleTime, prevKernelTime, prevUserTime;
	FILETIME currIdleTime, currKernelTime, currUserTime;

	GetSystemTimes(&prevIdleTime, &prevKernelTime, &prevUserTime);

	// Wait for a short interval to measure CPU activity
	std::this_thread::sleep_for(std::chrono::milliseconds(50));

	// Take second snapshot after the interval
	GetSystemTimes(&currIdleTime, &currKernelTime, &currUserTime);

	// Convert FILETIME structures to ULARGE_INTEGER for easier arithmetic
	ULARGE_INTEGER prevIdle, prevTotal;
	ULARGE_INTEGER currIdle, currTotal;

	prevIdle.LowPart = prevIdleTime.dwLowDateTime;
	prevIdle.HighPart = prevIdleTime.dwHighDateTime;

	prevTotal.LowPart = prevKernelTime.dwLowDateTime;
	prevTotal.HighPart = prevKernelTime.dwHighDateTime;

	currIdle.LowPart = currIdleTime.dwLowDateTime;
	currIdle.HighPart = currIdleTime.dwHighDateTime;

	currTotal.LowPart = currKernelTime.dwLowDateTime;
	currTotal.HighPart = currKernelTime.dwHighDateTime;

	// Get performance counter frequency for conversion to milliseconds
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);

	// Convert from 100-nanosecond units to milliseconds
	__int64 prevIdleMs = (prevIdle.QuadPart * 1000) / frequency.QuadPart;
	__int64 prevTotalMs = (prevTotal.QuadPart * 1000) / frequency.QuadPart;
	__int64 currIdleMs = (currIdle.QuadPart * 1000) / frequency.QuadPart;
	__int64 currTotalMs = (currTotal.QuadPart * 1000) / frequency.QuadPart;

	// Calculate the change in idle and total time during the interval
	__int64 idleDelta = currIdleMs - prevIdleMs;
	__int64 totalDelta = currTotalMs - prevTotalMs;

	if (totalDelta > 0) {
		ProcessorStats newStats;
		// CPU usage = 1 - (idle_time / total_time), expressed as percentage
		newStats.usagePercentage =
			(1.0 - (double)idleDelta / totalDelta) * 100.0;
		// Windows CPU temperature is intentionally left unavailable: there is no
		// clean, no-admin, accurate API (WMI MSAcpi_ThermalZoneTemperature needs
		// admin and is often a BIOS placeholder; accurate reads require a kernel
		// driver). temperatureC keeps the -1.0 sentinel -> UI renders "N/A".

		std::lock_guard<std::mutex> lock(statsMutex_);
		stats_ = newStats;
	}
}
