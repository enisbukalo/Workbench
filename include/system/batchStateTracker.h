#pragma once

#include "IBatchStateTracker.h"

#include <map>
#include <string>
#include <vector>

/**
 * @file batchStateTracker.h
 * @brief Derives a batch's load status from per-preset truth (issue #111).
 *
 * A batch is loaded exactly when every preset in it is loaded, so its status is
 * a pure function of @c ModelStateTracker's snapshot — this class holds no state
 * and talks to nothing. It exists only so the panel can depend on an interface
 * and be unit-tested with a mock.
 */
class BatchStateTracker : public IBatchStateTracker
{
  public:
	/** @brief Process-wide singleton (Meyers'). */
	static BatchStateTracker &instance()
	{
		static BatchStateTracker tracker;
		return tracker;
	}

	BatchStateTracker() = default;
	~BatchStateTracker() override = default;
	BatchStateTracker(const BatchStateTracker &) = delete;
	BatchStateTracker &operator=(const BatchStateTracker &) = delete;

	[[nodiscard]] BatchLifecycle
	statusOf(const std::vector<std::string> &presets,
			 const std::map<std::string, ModelState> &models) const override;
};
