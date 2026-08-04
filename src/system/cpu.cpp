/**
 * @file cpu.cpp
 * @brief CPU monitor platform dispatcher.
 *
 * Provides the platform-dispatched update() method that routes to
 * the appropriate Linux or Windows implementation based on compile-time
 * preprocessor definitions.
 */

#include "cpuMonitor.h"

void CpuMonitor::update()
{
#ifdef _WIN32
	updateWindows();
#elif __linux__
	updateLinux();
#else
	updateUnknown();
#endif
}
