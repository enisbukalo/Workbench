#pragma once

#include <optional>
#include <string>
#include <vector>

/**
 * @file IBatchStore.h
 * @brief Interface for the named-batch persistence store (issue #111).
 *
 * A *batch* is a named, ordered collection of preset (models.ini section)
 * names. Duplicates are allowed — the same preset may appear in a batch more
 * than once. A batch stores only names; the preset itself owns all
 * load/inference settings, so batches carry no model configuration.
 *
 * Panels and the control API depend on this interface rather than the concrete
 * @c BatchStore singleton, enabling unit testing with GMock. The real
 * @c BatchStore implements this interface directly (zero indirection overhead).
 */

/**
 * @brief A named, ordered list of preset section names.
 */
struct Batch
{
	/** @brief Unique batch name (the key in batches.json). */
	std::string name;

	/**
	 * @brief Ordered preset section names that make up this batch.
	 *
	 * Each entry is a models.ini section name (the canonical preset identifier
	 * used by @c loadModel / @c unloadModel). Duplicates are permitted and
	 * preserved in order.
	 */
	std::vector<std::string> presets;
};

class IBatchStore
{
  public:
	virtual ~IBatchStore() = default;

	/**
	 * @brief List all batch names.
	 * @return Sorted, distinct batch names.
	 */
	[[nodiscard]] virtual std::vector<std::string> getBatchNames() const = 0;

	/**
	 * @brief Fetch a batch by name.
	 * @param name Batch name.
	 * @return The batch, or @c std::nullopt if no batch has that name.
	 */
	[[nodiscard]] virtual std::optional<Batch>
	getBatch(const std::string &name) const = 0;

	/**
	 * @brief Persist a batch (upsert by name).
	 *
	 * An empty preset list is allowed (a valid, loadable no-op batch). Unknown
	 * section names are NOT rejected here; validity is checked at load time
	 * against the live preset list, since a referenced preset may be created
	 * later.
	 *
	 * @param batch Batch to write. A batch with an empty @c name is rejected.
	 * @return @c true on success, @c false on invalid input or write failure.
	 */
	virtual bool saveBatch(const Batch &batch) = 0;

	/**
	 * @brief Delete a batch by name.
	 * @param name Batch name.
	 * @return @c true on success (including idempotent delete of a missing
	 *         batch is treated as failure → returns @c false only on I/O error;
	 *         a missing batch returns @c false).
	 */
	virtual bool deleteBatch(const std::string &name) = 0;
};
