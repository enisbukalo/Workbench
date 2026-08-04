/**
 * @file systemInfoUtils.cpp
 * @brief System information utility functions.
 *
 * Provides cross-platform utilities for querying hardware information,
 * primarily GPU detection via nvidia-smi command-line tool.
 */

#include "systemInfoUtils.h"
#include <cstdio>
#include <string>

#ifdef _WIN32
#define POPEN _popen
#define PCLOSE _pclose
#define NULL_REDIRECT "2>nul"
#else
#define POPEN popen
#define PCLOSE pclose
#define NULL_REDIRECT "2>/dev/null"
#endif

std::vector<Hardware> SystemInfoUtils::getGpuInfo()
{
	std::vector<Hardware> gpus;

	// Execute nvidia-smi to get GPU names in CSV format
	// Redirect stderr to suppress errors if nvidia-smi is not available
	FILE *pipe =
		POPEN("nvidia-smi --query-gpu=name --format=csv,noheader " NULL_REDIRECT,
			  "r");
	if (!pipe)
		return gpus;

	char buffer[256];
	// Parse each line as a GPU name
	while (fgets(buffer, sizeof(buffer), pipe)) {
		std::string name(buffer);
		// Remove trailing newline characters (handle both Unix and Windows line
		// endings)
		if (!name.empty() && name.back() == '\n')
			name.pop_back();
		if (!name.empty() && name.back() == '\r')
			name.pop_back();
		if (name.empty())
			continue;

		// Split "NVIDIA GeForce RTX 3080" into make="NVIDIA" and model="GeForce
		// RTX 3080"
		auto space = name.find(' ');
		std::string make = name.substr(0, space);
		std::string model =
			(space != std::string::npos) ? name.substr(space + 1) : "";
		gpus.push_back({ HardwareType::GPU, make, model });
	}

	PCLOSE(pipe);
	return gpus;
}
