#pragma once

#include "IBatchStore.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

/**
 * @file batchStore.h
 * @brief JSON-backed persistence for named preset batches (issue #111).
 *
 * Loads and saves batches from @c ~/.workbench/batches.json. The file maps each
 * batch name to an ordered list of preset (models.ini section) names:
 *
 * @code{.json}
 * {
 *   "version": 1,
 *   "batches": {
 *     "smoke":  ["Qwen3.5-0.8B-Q8_0 MTP", "Qwen3.5-2B-Q4_K_XL MTP"],
 *     "dual08": ["Qwen3.5-0.8B-Q8_0 MTP", "Qwen3.5-0.8B-Q8_0 MTP"]
 *   }
 * }
 * @endcode
 *
 * Mirrors the @c ModelsIni singleton: a process-wide instance whose file path is
 * resolved from @c ConfigManager::getConfigDir() at @c load() time.
 */
class BatchStore : public IBatchStore
{
  public:
	/** @brief Get the singleton instance. */
	static BatchStore &instance();

	/**
	 * @brief Load batches.json from the config directory.
	 *
	 * A missing file yields an empty store (the file is created on the first
	 * @c saveBatch). Invalid JSON logs an error and yields an empty store
	 * (matching @c ConfigManager::load() fallback behavior).
	 *
	 * @return @c true if a file was read successfully, @c false if it was
	 *         missing or unreadable (the store is still usable/empty).
	 */
	bool load();

	/** @brief Absolute path to batches.json (valid after @c load()). */
	[[nodiscard]] std::string getPath() const;

	[[nodiscard]] std::vector<std::string> getBatchNames() const override;
	[[nodiscard]] std::optional<Batch>
	getBatch(const std::string &name) const override;
	bool saveBatch(const Batch &batch) override;
	bool deleteBatch(const std::string &name) override;

  private:
	BatchStore() = default;
	~BatchStore() = default;
	BatchStore(const BatchStore &) = delete;
	BatchStore &operator=(const BatchStore &) = delete;

	/**
	 * @brief Serialize @c m_batches to batches.json atomically.
	 *
	 * Writes to a temp file then renames over the target so a crash mid-write
	 * never corrupts the existing file.
	 *
	 * @return @c true on success.
	 */
	bool persist() const;

	std::string m_filePath;
	/// Batch name -> ordered preset section names.
	std::map<std::string, std::vector<std::string>> m_batches;
};
