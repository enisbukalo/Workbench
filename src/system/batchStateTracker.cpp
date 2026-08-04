#include "batchStateTracker.h"

BatchLifecycle BatchStateTracker::statusOf(
	const std::vector<std::string> &presets,
	const std::map<std::string, ModelState> &models) const
{
	if (presets.empty())
		return BatchLifecycle::UNLOADED;

	bool anyActive = false; // at least one LOADED or LOADING
	bool allLoaded = true;	// every preset LOADED
	for (const auto &section : presets) {
		const auto it = models.find(section);
		const bool loaded =
			it != models.end() && it->second.lifecycle == ModelLifecycle::LOADED;
		const bool loading = it != models.end() &&
							 it->second.lifecycle == ModelLifecycle::LOADING;
		if (!loaded)
			allLoaded = false;
		if (loaded || loading)
			anyActive = true;
	}

	if (allLoaded)
		return BatchLifecycle::LOADED;
	if (anyActive)
		return BatchLifecycle::LOADING;
	return BatchLifecycle::UNLOADED;
}
