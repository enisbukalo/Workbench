#pragma once

#include "modelState.h"

#include <map>
#include <string>
#include <vector>

/**
 * @file IBatchStateTracker.h
 * @brief Interface deriving a batch's load status from per-preset truth (issue
 *        #111).
 *
 * A batch is loaded exactly when every preset in it is loaded. Since presets are
 * unique server-side (the router routes by a unique id == the models.ini section
 * name, so the same preset can never be loaded twice), a batch's status is fully
 * DERIVED from @c IModelStateTracker — there is no separate batch intent to
 * store. This is a pure (stateless) function over the model snapshot, kept
 * behind an interface so the panel can be unit-tested with a mock.
 *
 * Two batches that share a preset stay consistent automatically: both reflect
 * the single per-preset truth. A preset loaded individually (not via the batch)
 * still counts toward its batch's status.
 */

/**
 * @enum BatchLifecycle
 * @brief Where a batch sits in its load lifecycle (derived from its presets).
 */
enum class BatchLifecycle
{
	UNLOADED, ///< No preset in the batch is loaded.
	LOADING,  ///< Some (but not all) presets are LOADED/LOADING.
	LOADED	  ///< Every preset in the batch is LOADED.
};

class IBatchStateTracker
{
  public:
	virtual ~IBatchStateTracker() = default;

	/**
	 * @brief Derive a batch's load status from the per-preset lifecycle map.
	 *
	 * Pure function of its inputs — holds no state:
	 *  - @c LOADED   when @p presets is non-empty AND every preset is present in
	 *    @p models with lifecycle @c LOADED.
	 *  - @c LOADING  when at least one preset is LOADED or LOADING but not all
	 *    presets are LOADED.
	 *  - @c UNLOADED otherwise (empty batch, or no preset loaded/loading).
	 *
	 * @param presets The batch's preset section names.
	 * @param models  Per-model lifecycle snapshot (ModelStateTracker::snapshot).
	 * @return The batch's lifecycle.
	 */
	[[nodiscard]] virtual BatchLifecycle
	statusOf(const std::vector<std::string> &presets,
			 const std::map<std::string, ModelState> &models) const = 0;
};
