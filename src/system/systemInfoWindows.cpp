/**
 * @file systemInfoWindows.cpp
 * @brief Windows-specific system information implementation.
 *
 * Implements CPU and GPU detection for Windows using the Windows Registry
 * and nvidia-smi command-line tool.
 */

#include "systemInfo.h"
#include "systemInfoUtils.h"
#include <string>
#include <windows.h>

Hardware SystemInfo::getCpuInfo()
{
	std::string modelName;

	// Open Windows Registry key for CPU information
	HKEY key;
	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
					  "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
					  0,
					  KEY_READ,
					  &key) == ERROR_SUCCESS) {

		char buffer[256];
		DWORD size = sizeof(buffer);
		// Read ProcessorNameString value from registry
		if (RegQueryValueExA(key,
							 "ProcessorNameString",
							 nullptr,
							 nullptr,
							 reinterpret_cast<LPBYTE>(buffer),
							 &size) == ERROR_SUCCESS) {
			modelName = buffer;
		}
		RegCloseKey(key);
	}

	// Parse "Intel Core i9-13900K" into make="Intel" and model="Core i9-13900K"
	std::string make = "Unknown";
	std::string model = "Unknown";
	if (!modelName.empty()) {
		auto space = modelName.find(' ');
		make = modelName.substr(0, space);
		model = (space != std::string::npos) ? modelName.substr(space + 1) : "";
	}

	return { HardwareType::CPU, make, model };
}

void SystemInfo::updateWindows()
{
	cpu_ = getCpuInfo();
	gpus_ = SystemInfoUtils::getGpuInfo();
}

void SystemInfo::update()
{
	updateWindows();
}
