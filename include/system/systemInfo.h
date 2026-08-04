#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/**
 * @file systemInfo.h
 * @brief Hardware detection and identification for Workbench.
 *
 * This header provides the core data structures for hardware identification,
 * including CPU and GPU detection. The SystemInfo singleton class uses
 * platform-specific implementations to query hardware information and
 * populate these structures.
 */

/**
 * @brief Discriminates between CPU and GPU hardware entries.
 *
 * This enumeration is used to distinguish between central processing units
 * and graphics processing units in the hardware inventory.
 */
enum class HardwareType
{
	CPU, ///< Central processing unit.
	GPU	 ///< Graphics processing unit.
};

/**
 * @brief Identifies a hardware device by type, manufacturer, and model name.
 *
 * This structure holds the essential identification information for any
 * detected hardware device. The make and model strings are parsed from
 * platform-specific sources (e.g., /proc/cpuinfo on Linux, Registry on Windows).
 */
struct Hardware
{
	HardwareType type; ///< Whether this entry describes a CPU or GPU.
	std::string make;  ///< Manufacturer name (e.g. "Intel", "NVIDIA").
	std::string model; ///< Model / product name (e.g. "Core i9-13900K").
};

/**
 * @brief Singleton that detects and stores CPU and GPU hardware identifiers.
 *
 * Hardware detection is platform-dispatched at runtime based on compile-time
 * platform detection macros. The singleton pattern ensures a single source of
 * truth for hardware information throughout the application.
 *
 * Platform-specific implementations:
 * - Linux: Parses /proc/cpuinfo for CPU, nvidia-smi for GPUs
 * - Windows: Queries Registry for CPU, nvidia-smi for GPUs
 * - Unknown: Returns default "Unknown" values
 *
 * Usage pattern:
 * @code
 * SystemInfo::instance().update();
 * auto cpu = SystemInfo::instance().getCpu();
 * auto gpus = SystemInfo::instance().getGpus();
 * @endcode
 *
 * @note The update() method must be called before accessing hardware data.
 *       Subsequent calls to getCpu() and getGpus() are thread-safe
 *       read-only operations.
 */
class SystemInfo
{
  public:
	/**
	 * @brief Returns the process-wide singleton instance.
	 *
	 * This implements the Meyers' singleton pattern using a function-local
	 * static variable, which guarantees thread-safe lazy initialization in
	 * C++11 and later. The instance is created on first call and persists
	 * for the lifetime of the program.
	 *
	 * @return Reference to the single @c SystemInfo object.
	 * @note The instance is lazily initialized on first call.
	 */
	static SystemInfo &instance()
	{
		static SystemInfo info;
		return info;
	}

	/**
	 * @brief Detects hardware on the current platform and populates internal
	 * state.
	 *
	 * This method queries the operating system for CPU and GPU information
	 * using platform-specific mechanisms:
	 * - On Linux: Reads /proc/cpuinfo and executes nvidia-smi
	 * - On Windows: Queries the Registry and executes nvidia-smi
	 * - On other platforms: Returns default "Unknown" values
	 *
	 * The detected hardware information is stored in internal member variables
	 * and can be accessed via getCpu() and getGpus() until the next update().
	 *
	 * @note This method should be called before accessing hardware data.
	 *       It is safe to call multiple times; subsequent calls will refresh
	 *       the cached hardware information.
	 */
	void update();

	/**
	 * @brief Returns the detected CPU descriptor.
	 *
	 * @return A @c Hardware value whose @c type is @c HardwareType::CPU.
	 *         The make and model fields contain the manufacturer and model
	 *         name respectively, or "Unknown" if detection failed.
	 * @note This method is thread-safe and can be called concurrently with
	 *       update() or other get_* methods.
	 * @see getGpus()
	 */
	Hardware getCpu() const
	{
		return cpu_;
	}

	/**
	 * @brief Returns all detected GPU descriptors.
	 *
	 * @return A vector of @c Hardware values whose @c type is @c
	 * HardwareType::GPU. The vector contains one entry per detected GPU,
	 * ordered by device index. Returns an empty vector if no GPUs were
	 * detected or if nvidia-smi is unavailable.
	 * @note This method is thread-safe and can be called concurrently with
	 *       update() or other get_* methods.
	 * @see getCpu()
	 */
	std::vector<Hardware> getGpus() const
	{
		return gpus_;
	}

	/**
	 * @brief Attempts a hardware detection update and returns the CPU
	 * descriptor.
	 *
	 * This convenience method calls @ref update() and immediately returns the
	 * detected CPU information. It provides a quick way to check if hardware
	 * detection is working without storing the result separately.
	 *
	 * @return The CPU @c Hardware descriptor. The make and model fields will
	 *         contain "Unknown" if detection failed or no CPU information
	 *         was available.
	 * @note Unlike getCpu(), this method does not return std::nullopt on
	 *       failure; it returns a Hardware struct with "Unknown" values.
	 * @see update(), getCpu()
	 */
	std::optional<Hardware> tryUpdate();

  private:
	/**
	 * @brief Private default constructor for singleton pattern.
	 *
	 * The SystemInfo class can only be instantiated through the instance()
	 * static method, which uses Meyers' singleton pattern.
	 */
	SystemInfo() = default;

	Hardware cpu_;
	std::vector<Hardware> gpus_;

	/**
	 * @brief Linux-specific hardware detection implementation.
	 *
	 * Parses /proc/cpuinfo for CPU information and executes nvidia-smi
	 * for GPU detection.
	 */
	void updateLinux();

	/**
	 * @brief Windows-specific hardware detection implementation.
	 *
	 * Queries the Windows Registry for CPU information and executes
	 * nvidia-smi for GPU detection.
	 */
	void updateWindows();

	/**
	 * @brief Fallback implementation for unknown platforms.
	 *
	 * Sets CPU and GPU information to "Unknown" values. This method
	 * is called on platforms that are not explicitly supported.
	 */
	void updateUnknown();

	/**
	 * @brief Extracts CPU information from platform-specific source.
	 *
	 * This method is called by the platform-specific update() methods
	 * to populate the cpu_ member variable with detected CPU information.
	 *
	 * @return Hardware struct containing the detected CPU's type, make,
	 *         and model. Returns "Unknown" values if detection fails.
	 */
	Hardware getCpuInfo();
};
