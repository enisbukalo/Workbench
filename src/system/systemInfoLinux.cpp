/**
 * @file systemInfoLinux.cpp
 * @brief Linux-specific system information implementation.
 *
 * Implements CPU and GPU detection for Linux by parsing /proc/cpuinfo
 * and using nvidia-smi command-line tool.
 */

#include "systemInfo.h"
#include "systemInfoUtils.h"
#include <fstream>
#include <string>

Hardware SystemInfo::getCpuInfo()
{
	std::ifstream file("/proc/cpuinfo");

	std::string modelName;

	// Helper lambda to trim whitespace from strings
	auto trim = [](const std::string &s) {
		auto start = s.find_first_not_of(" \t");
		auto end = s.find_last_not_of(" \t");
		return (start == std::string::npos) ? ""
											: s.substr(start, end - start + 1);
	};

	// Parse /proc/cpuinfo line by line
	for (std::string line; std::getline(file, line);) {
		auto colon = line.find(':');
		if (colon == std::string::npos)
			continue;

		std::string key = trim(line.substr(0, colon));
		std::string value = trim(line.substr(colon + 1));

		// Extract the "model name" field
		if (key == "model name") {
			modelName = value;
			break;
		}
	}

	// Parse "Intel(R) Core(TM) i9-13900K CPU @ 3.00GHz" into make and model
	std::string make = "Unknown";
	std::string model = "Unknown";
	if (!modelName.empty()) {
		auto space = modelName.find(' ');
		make = modelName.substr(0, space);
		model = (space != std::string::npos) ? modelName.substr(space + 1) : "";
	}

	return { HardwareType::CPU, make, model };
}

void SystemInfo::updateLinux()
{
	cpu_ = getCpuInfo();
	gpus_ = SystemInfoUtils::getGpuInfo();
}

void SystemInfo::update()
{
	updateLinux();
}
