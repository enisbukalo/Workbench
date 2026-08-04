#include "batchStore.h"

#include "configManager.h"
#include "json.hpp"

#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

BatchStore &BatchStore::instance()
{
	static BatchStore instance;
	return instance;
}

std::string BatchStore::getPath() const
{
	return m_filePath;
}

bool BatchStore::load()
{
	const std::string configDir = ConfigManager::instance().getConfigDir();
	m_filePath = configDir + "/batches.json";
	m_batches.clear();

	std::ifstream file(m_filePath);
	if (!file.is_open()) {
		spdlog::debug("batches.json not found at '{}' (none defined yet)",
					  m_filePath);
		return false;
	}

	try {
		const json j = json::parse(file);
		if (j.contains("batches") && j["batches"].is_object()) {
			for (const auto &[name, presets] : j["batches"].items()) {
				if (!presets.is_array())
					continue;
				std::vector<std::string> sections;
				for (const auto &p : presets) {
					if (p.is_string())
						sections.push_back(p.get<std::string>());
				}
				m_batches[name] = std::move(sections);
			}
		}
	} catch (const std::exception &e) {
		// Corrupt file: fall back to empty store rather than crashing, mirroring
		// ConfigManager's invalid-JSON handling.
		spdlog::error(
			"Failed to parse batches.json '{}': {} — using empty store",
			m_filePath,
			e.what());
		m_batches.clear();
		return false;
	}

	spdlog::info("Loaded {} batch(es) from batches.json", m_batches.size());
	return true;
}

std::vector<std::string> BatchStore::getBatchNames() const
{
	// std::map iterates in sorted key order; keys are distinct by construction.
	std::vector<std::string> names;
	names.reserve(m_batches.size());
	for (const auto &[name, _] : m_batches)
		names.push_back(name);
	return names;
}

std::optional<Batch> BatchStore::getBatch(const std::string &name) const
{
	const auto it = m_batches.find(name);
	if (it == m_batches.end())
		return std::nullopt;
	return Batch{ it->first, it->second };
}

bool BatchStore::saveBatch(const Batch &batch)
{
	if (batch.name.empty()) {
		spdlog::error("Cannot save batch with empty name");
		return false;
	}
	// Upsert; duplicate preset entries are preserved verbatim.
	m_batches[batch.name] = batch.presets;
	if (!persist()) {
		spdlog::error("Failed to persist batch '{}'", batch.name);
		return false;
	}
	spdlog::info("Saved batch '{}' ({} preset(s))",
				 batch.name,
				 batch.presets.size());
	return true;
}

bool BatchStore::deleteBatch(const std::string &name)
{
	const auto it = m_batches.find(name);
	if (it == m_batches.end()) {
		spdlog::warn("Batch '{}' not found for deletion", name);
		return false;
	}
	m_batches.erase(it);
	if (!persist()) {
		spdlog::error("Failed to persist after deleting batch '{}'", name);
		return false;
	}
	spdlog::info("Deleted batch '{}'", name);
	return true;
}

bool BatchStore::persist() const
{
	if (m_filePath.empty()) {
		spdlog::error("BatchStore::persist called before load() set a path");
		return false;
	}

	json j;
	j["version"] = 1;
	j["batches"] = json::object();
	for (const auto &[name, presets] : m_batches)
		j["batches"][name] = presets;

	// Atomic write: temp file + rename, so a crash mid-write never corrupts the
	// existing batches.json.
	const std::string tmpPath = m_filePath + ".tmp";
	{
		std::ofstream out(tmpPath);
		if (!out.is_open()) {
			spdlog::error("Failed to open temp file for writing: {}", tmpPath);
			return false;
		}
		out << j.dump(4) << "\n";
	}

	std::error_code ec;
	std::filesystem::rename(tmpPath, m_filePath, ec);
	if (ec) {
		spdlog::error("Failed to rename temp batches file: {}", ec.message());
		std::filesystem::remove(tmpPath, ec);
		return false;
	}
	return true;
}
