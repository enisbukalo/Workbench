/**
 * @file cpuLinux.cpp
 * @brief Linux-specific CPU monitoring implementation.
 *
 * Parses /proc/stat to extract CPU time statistics and calculates
 * utilization by comparing idle time deltas over a 100ms sampling interval.
 */

#include "cpuMonitor.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>

namespace {

/**
 * @brief Best-effort read of the package CPU temperature in degrees Celsius.
 *
 * Probes Linux sysfs without root: first the hwmon interface for a known CPU
 * sensor (coretemp/k10temp/zenpower), preferring the "Package id 0"/"Tctl"
 * input; then falls back to the x86_pkg_temp thermal zone. Values in sysfs are
 * milli-°C and are scaled by 1/1000. Returns -1.0 on any failure so the caller
 * can carry the ProcessorStats "unavailable" sentinel.
 */
double readCpuTempC()
{
	namespace fs = std::filesystem;

	auto readMilliC = [](const fs::path &p, double &out) -> bool {
		std::ifstream f(p);
		long long milli = 0;
		if (f >> milli) {
			out = milli / 1000.0;
			return true;
		}
		return false;
	};

	std::error_code ec;

	// 1) hwmon: find a CPU sensor, then read its package input.
	const fs::path hwmonRoot{ "/sys/class/hwmon" };
	if (fs::exists(hwmonRoot, ec)) {
		for (const auto &entry : fs::directory_iterator(
				 hwmonRoot,
				 fs::directory_options::skip_permission_denied,
				 ec)) {
			if (ec)
				break;

			std::string sensorName;
			{
				std::ifstream nameFile(entry.path() / "name");
				std::getline(nameFile, sensorName);
			}
			// Strip trailing whitespace / CR (WSL, trailing newline)
			while (!sensorName.empty() &&
				   (sensorName.back() == '\r' || sensorName.back() == '\n' ||
					sensorName.back() == ' ')) {
				sensorName.pop_back();
			}
			// Prefix match: k10temp-pci-0000:03:01.8 still matches "k10temp"
			auto isCpuSensor = [](const std::string &name) {
				return name.rfind("coretemp", 0) == 0 ||
					   name.rfind("k10temp", 0) == 0 ||
					   name.rfind("zenpower", 0) == 0;
			};
			if (!isCpuSensor(sensorName)) {
				continue;
			}

			// Prefer the tempN_input whose tempN_label is the package reading.
			double packageTemp = -1.0;
			double firstTemp = -1.0;
			for (const auto &file : fs::directory_iterator(
					 entry.path(),
					 fs::directory_options::skip_permission_denied,
					 ec)) {
				if (ec)
					break;
				const std::string fname = file.path().filename().string();
				const std::string suffix = "_input";
				if (fname.size() <= suffix.size() ||
					fname.compare(fname.size() - suffix.size(),
								  suffix.size(),
								  suffix) != 0 ||
					fname.rfind("temp", 0) != 0) {
					continue;
				}

				double value = -1.0;
				if (!readMilliC(file.path(), value)) {
					continue;
				}
				if (firstTemp < 0.0) {
					firstTemp = value; // fallback: lowest-numbered input
				}

				// Sibling label: tempN_label for this tempN_input.
				const std::string stem =
					fname.substr(0, fname.size() - suffix.size());
				std::string label;
				{
					std::ifstream labelFile(entry.path() / (stem + "_label"));
					std::getline(labelFile, label);
				}
				if (label == "Package id 0" || label == "Tctl") {
					packageTemp = value;
					break;
				}
			}

			if (packageTemp >= 0.0)
				return packageTemp;
			if (firstTemp >= 0.0)
				return firstTemp;
		}
	}

	// 2) thermal zone fallback: x86_pkg_temp.
	const fs::path thermalRoot{ "/sys/class/thermal" };
	if (fs::exists(thermalRoot, ec)) {
		for (const auto &entry : fs::directory_iterator(
				 thermalRoot,
				 fs::directory_options::skip_permission_denied,
				 ec)) {
			if (ec)
				break;

			std::string zoneType;
			{
				std::ifstream typeFile(entry.path() / "type");
				std::getline(typeFile, zoneType);
			}
			if (zoneType != "x86_pkg_temp")
				continue;

			double value = -1.0;
			if (readMilliC(entry.path() / "temp", value))
				return value;
		}
	}

	// 3) No match -> unavailable.
	return -1.0;
}

} // namespace

void CpuMonitor::updateLinux()
{
	std::ifstream file("/proc/stat");

	long long prevIdle = 0, prevTotal = 0;

	std::string line;
	// Read first snapshot from /proc/stat
	if (std::getline(file, line)) {
		std::istringstream iss(line);
		std::string cpu;
		iss >> cpu;

		// Parse CPU time fields: user, nice, system, idle, iowait, irq, softirq,
		// steal
		long long user, nice, system, idle, iowait, irq, softirq, steal;
		iss >> user >> nice >> system >> idle >> iowait >> irq >> softirq >>
			steal;

		// Idle time includes both idle and iowait states
		prevIdle = idle + iowait;
		// Total time is sum of all CPU time fields
		prevTotal = user + nice + system + idle + iowait + irq + softirq + steal;
	}

	// Wait for a short interval to measure CPU activity
	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	// Reset file stream and re-read
	file.clear();
	file.seekg(0);

	long long currIdle = 0, currTotal = 0;

	// Read second snapshot
	if (std::getline(file, line)) {
		std::istringstream iss(line);
		std::string cpu;
		iss >> cpu;

		long long user, nice, system, idle, iowait, irq, softirq, steal;
		iss >> user >> nice >> system >> idle >> iowait >> irq >> softirq >>
			steal;

		currIdle = idle + iowait;
		currTotal = user + nice + system + idle + iowait + irq + softirq + steal;
	}

	// Calculate the change in idle and total time during the interval
	long long idleDelta = currIdle - prevIdle;
	long long totalDelta = currTotal - prevTotal;

	if (totalDelta > 0) {
		ProcessorStats newStats;
		// CPU usage = 1 - (idle_time / total_time), expressed as percentage
		newStats.usagePercentage =
			(1.0 - (double)idleDelta / totalDelta) * 100.0;

		std::lock_guard<std::mutex> lock(statsMutex_);
		// Best-effort temperature; smoothed into a rolling 60s average so the UI
		// shows a stable number instead of the noisy per-sample sysfs reading.
		// readCpuTempC() returns the -1.0 sentinel when no sensor is present,
		// which the averager passes through unchanged (UI renders "N/A").
		newStats.temperatureC = tempAverager_.add(readCpuTempC());
		stats_ = newStats;
	}
}
