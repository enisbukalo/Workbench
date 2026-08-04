#include "batchStateTracker.h"

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

namespace {

// Build a per-model lifecycle snapshot from (section, lifecycle) pairs.
std::map<std::string, ModelState>
snapshot(std::initializer_list<std::pair<std::string, ModelLifecycle>> entries)
{
	std::map<std::string, ModelState> m;
	for (const auto &[id, lc] : entries) {
		ModelState s;
		s.id = id;
		s.lifecycle = lc;
		m[id] = s;
	}
	return m;
}

} // namespace

TEST(BatchStateTrackerTest, EmptyPresets_Unloaded)
{
	BatchStateTracker t;
	EXPECT_EQ(t.statusOf({}, {}), BatchLifecycle::UNLOADED);
}

TEST(BatchStateTrackerTest, AllPresetsLoaded_Loaded)
{
	BatchStateTracker t;
	const auto snap = snapshot({ { "a", ModelLifecycle::LOADED },
								 { "b", ModelLifecycle::LOADED } });
	EXPECT_EQ(t.statusOf({ "a", "b" }, snap), BatchLifecycle::LOADED);
}

TEST(BatchStateTrackerTest, SomeLoaded_Loading)
{
	BatchStateTracker t;
	const auto snap = snapshot({ { "a", ModelLifecycle::LOADED },
								 { "b", ModelLifecycle::LOADING } });
	EXPECT_EQ(t.statusOf({ "a", "b" }, snap), BatchLifecycle::LOADING);
}

TEST(BatchStateTrackerTest, PresetAbsentFromSnapshot_Loading)
{
	BatchStateTracker t;
	// "a" loaded, "b" not present at all -> not all loaded, but one active.
	const auto snap = snapshot({ { "a", ModelLifecycle::LOADED } });
	EXPECT_EQ(t.statusOf({ "a", "b" }, snap), BatchLifecycle::LOADING);
}

TEST(BatchStateTrackerTest, NonePresent_Unloaded)
{
	BatchStateTracker t;
	EXPECT_EQ(t.statusOf({ "a", "b" }, {}), BatchLifecycle::UNLOADED);
}

TEST(BatchStateTrackerTest, AllUnloadingOrGone_Unloaded)
{
	BatchStateTracker t;
	// GONE/UNLOADING count as neither LOADED nor LOADING -> not active.
	const auto snap = snapshot({ { "a", ModelLifecycle::GONE },
								 { "b", ModelLifecycle::UNLOADING } });
	EXPECT_EQ(t.statusOf({ "a", "b" }, snap), BatchLifecycle::UNLOADED);
}

TEST(BatchStateTrackerTest, SharedPreset_DerivedConsistently)
{
	// Batches A=[p1,p2] and B=[p1,p3]. p1,p2 loaded; p3 not.
	BatchStateTracker t;
	const auto snap = snapshot({ { "p1", ModelLifecycle::LOADED },
								 { "p2", ModelLifecycle::LOADED } });
	EXPECT_EQ(t.statusOf({ "p1", "p2" }, snap), BatchLifecycle::LOADED);
	// B shares p1 (loaded) but p3 missing -> LOADING (one active, not all).
	EXPECT_EQ(t.statusOf({ "p1", "p3" }, snap), BatchLifecycle::LOADING);
}
