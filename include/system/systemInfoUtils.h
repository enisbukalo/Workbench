#pragma once
#include "systemInfo.h"
#include <vector>

/**
 * @file systemInfoUtils.h
 * @brief Utility functions for querying hardware information from the system.
 *
 * This namespace provides platform-independent utility functions for
 * hardware detection. The primary function, getGpuInfo(), queries
 * NVIDIA GPUs using the nvidia-smi command-line tool.
 */
namespace SystemInfoUtils {

/**
 * @brief Runs @c nvidia-smi to enumerate GPU make and model strings.
 *
 * This function executes the nvidia-smi command-line tool with the
 * --query-gpu=name option to retrieve the model names of all detected
 * NVIDIA GPUs. The output is parsed line-by-line, with each line
 * representing one GPU.
 *
 * The function handles the case where nvidia-smi is unavailable by
 * redirecting stderr to null and returning an empty vector. This
 * allows the application to gracefully handle systems without NVIDIA
 * GPUs or without the nvidia-smi utility installed.
 *
 * Parsing logic:
 * - Each line in the CSV output is treated as a GPU name
 * - The name is split on the first space to separate manufacturer
 *   (e.g., "NVIDIA") from model (e.g., "GeForce RTX 3080")
 * - Both Unix and Windows line endings are handled
 *
 * @return A vector of @c Hardware entries with @c type set to
 *         @c HardwareType::GPU, one entry per detected GPU.
 *         The vector is empty if:
 *         - nvidia-smi is not available or not in PATH
 *         - No NVIDIA GPUs are detected
 *         - The command fails or returns unexpected output
 *
 * @note This function requires the nvidia-smi utility to be available
 *       in the system PATH. On systems without NVIDIA GPUs or drivers,
 *       the function will return an empty vector without error.
 */
std::vector<Hardware> getGpuInfo();

} // namespace SystemInfoUtils
