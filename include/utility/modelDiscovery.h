#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <chrono>

/**
 * @file modelDiscovery.h
 * @brief Model file discovery utility for scanning directories for .gguf files.
 *
 * This utility provides functionality to recursively scan configured
 * directories for GGUF model files, cache the results, and provide
 * user-friendly display names for models.
 *
 * @code
 * // Scan all configured search paths
 * auto models = ModelDiscovery::instance().scanForModels();
 *
 * // Get cached results (if not stale)
 * auto cached = ModelDiscovery::instance().getCachedModels();
 *
 * // Convert path to display name
 * std::string name = ModelDiscovery::instance()
 *     .pathToDisplayName("/models/Qwen3.5-27B.Q6_K.gguf");
 * // Returns: "Qwen3.5-27B.Q6_K"
 * @endcode
 *
 * @see DiscoverySettings for configuring search paths
 */
class ModelDiscovery
{
public:
	/**
	 * @brief Scan all configured search paths for model files.
	 *
	 * Iterates through all paths in DiscoverySettings::modelSearchPaths
	 * and collects all .gguf files found. Results are cached for
	 * CACHE_TTL duration.
	 *
	 * @return Vector of full paths to discovered model files
	 * @note Empty if no search paths are configured or none exist
	 * @see DiscoverySettings::modelSearchPaths
	 */
	std::vector<std::string> scanForModels();

	/**
	 * @brief Scan a single directory recursively for model files.
	 *
	 * Recursively traverses the specified directory looking for files
	 * with .gguf extension (case-insensitive). Expands tilde (~) to
	 * home directory on POSIX systems.
	 *
	 * @param dirPath Directory path to scan
	 * @return Vector of full paths to .gguf files found
	 * @note Returns empty vector if directory doesn't exist
	 * @note Non-existent directories are silently skipped
	 */
	std::vector<std::string> scanDirectory(const std::string &dirPath);

	/**
	 * @brief Convert a full model path to a user-friendly display name.
	 *
	 * Extracts the filename without extension from the full path.
	 * For example: "/path/to/Qwen3.5-27B.Q6_K.gguf" -> "Qwen3.5-27B.Q6_K"
	 *
	 * @param fullPath Full path to a model file
	 * @return Display name (filename without extension)
	 */
	std::string pathToDisplayName(const std::string &fullPath);

	/**
	 * @brief Get cached model list if not stale.
	 *
	 * Returns the cached results from the last scan if CACHE_TTL
	 * has not elapsed. Otherwise returns empty vector.
	 *
	 * @return Cached model paths if fresh, empty vector otherwise
	 * @see CACHE_TTL for cache duration
	 */
	std::vector<std::string> getCachedModels();

	/**
	 * @brief Force refresh the model cache.
	 *
	 * Clears existing cache and performs a new scan of all
	 * configured search paths.
	 *
	 * @return Vector of full paths to discovered model files
	 */
	std::vector<std::string> refreshCache();

	/**
	 * @brief Get the singleton instance.
	 *
	 * Uses Meyers' Singleton pattern for thread-safe, lazy initialization.
	 *
	 * @return Reference to the singleton ModelDiscovery instance
	 */
	static ModelDiscovery &instance();

private:
	ModelDiscovery() = default;
	~ModelDiscovery() = default;
	ModelDiscovery(const ModelDiscovery &) = delete;
	ModelDiscovery &operator=(const ModelDiscovery &) = delete;

	/** Expand tilde (~) to home directory. */
	std::string expandTilde(const std::string &path);

	/** Thread-safe access to cached models. */
	mutable std::mutex m_mutex;

	/** Cached list of discovered model paths. */
	std::vector<std::string> m_cachedModels;

	/** Timestamp of last cache update. */
	std::chrono::system_clock::time_point m_cacheTime{};

	/** Cache duration before forcing rescan (5 minutes). */
	static constexpr auto CACHE_TTL = std::chrono::minutes{5};
};
