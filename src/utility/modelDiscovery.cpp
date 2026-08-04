#include "modelDiscovery.h"
#include "configManager.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

std::string ModelDiscovery::expandTilde(const std::string &path)
{
	if (path.empty() || path[0] != '~') {
		return path;
	}

#ifdef _WIN32
	// Windows: Use USERPROFILE or HOMEPATH/HOMEDRIVE
	const char *home = std::getenv("USERPROFILE");
	if (!home || !*home) {
		home = std::getenv("HOMEDRIVE");
		const char *homepath = std::getenv("HOMEPATH");
		if (home && homepath) {
			std::string combined = std::string(home) + homepath;
			return combined + path.substr(1);
		}
	}
#else
	// POSIX: Use HOME
	const char *home = std::getenv("HOME");
#endif

	if (home && *home) {
		return std::string(home) + path.substr(1);
	}

	// Fallback: return path as-is if home not found
	spdlog::debug("Could not expand ~, using as-is");
	return path;
}

std::vector<std::string>
ModelDiscovery::scanDirectory(const std::string &dirPath)
{
	std::vector<std::string> models;

	// Expand ~ to home directory
	std::string expandedPath = expandTilde(dirPath);

	// Skip if directory doesn't exist or isn't a directory.
	// Use warn (not debug) so a misconfigured search path is visible to the
	// user — otherwise an empty model list looks like there are simply no
	// models, with no hint that the configured path is wrong.
	if (!fs::is_directory(expandedPath)) {
		spdlog::warn("Model search path does not exist or is not a directory: "
					 "'{}'",
					 expandedPath);
		return models;
	}

	// Recursive directory traversal
	for (const auto &entry : fs::recursive_directory_iterator(expandedPath)) {
		if (entry.is_regular_file()) {
			std::string fullPath = entry.path().string();

			// Check for .gguf extension (case-insensitive)
			std::string ext = entry.path().extension().string();
			std::transform(ext.begin(), ext.end(), ext.begin(), ::toupper);

			if (ext == ".GGUF") {
				models.push_back(fullPath);
			}
		}
	}

	spdlog::debug("Found {} model(s) in '{}'", models.size(), expandedPath);
	return models;
}

std::string ModelDiscovery::pathToDisplayName(const std::string &fullPath)
{
	fs::path p(fullPath);

	// Extract filename without extension
	// e.g., "/path/to/Qwen3.5-27B.Q6_K.gguf" -> "Qwen3.5-27B.Q6_K"
	return p.stem().string();
}

std::vector<std::string> ModelDiscovery::getCachedModels()
{
	std::lock_guard<std::mutex> lock(m_mutex);

	// Check if cache is stale
	if (std::chrono::duration_cast<std::chrono::minutes>(
			std::chrono::system_clock::now() - m_cacheTime)
			.count() >= CACHE_TTL.count() / std::chrono::minutes::period::den) {
		return {};
	}

	return m_cachedModels;
}

std::vector<std::string> ModelDiscovery::scanForModels()
{
	// Check cache first
	auto cached = getCachedModels();
	if (!cached.empty()) {
		spdlog::debug("Model cache hit, returning {} cached model(s)",
					  cached.size());
		return cached;
	}

	// Cache miss or stale - perform new scan
	spdlog::debug("Model cache miss, performing fresh scan");
	return refreshCache();
}

std::vector<std::string> ModelDiscovery::refreshCache()
{
	std::lock_guard<std::mutex> lock(m_mutex);

	std::vector<std::string> allModels;

	// Get configured search path from config
	const auto cfg = ConfigManager::instance().getConfigSnapshot();
	const auto &searchPath = cfg.discovery.modelSearchPath;

	spdlog::debug("Scanning model search path: '{}'", searchPath);

	// Scan the configured path if non-empty
	if (!searchPath.empty()) {
		auto models = scanDirectory(searchPath);
		allModels.insert(allModels.end(), models.begin(), models.end());
	}

	// Remove duplicates while preserving order
	std::sort(allModels.begin(), allModels.end());
	allModels.erase(std::unique(allModels.begin(), allModels.end()),
					allModels.end());

	// Update cache
	m_cachedModels = allModels;
	m_cacheTime = std::chrono::system_clock::now();

	spdlog::info("Model scan complete: {} model(s) found", allModels.size());

	return m_cachedModels;
}

ModelDiscovery &ModelDiscovery::instance()
{
	static ModelDiscovery instance;
	return instance;
}
