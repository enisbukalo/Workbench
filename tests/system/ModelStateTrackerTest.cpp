/**
 * @file ModelStateTrackerTest.cpp
 * @brief Unit tests for the model lifecycle state machine (#110).
 *
 * Pure tests — no network. The probe/gatherStats callables are supplied inline,
 * so every transition is driven deterministically. The central regression that
 * motivated the tracker is pinned here: an intentional unload that later
 * disappears becomes GONE, never CRASHED, regardless of poll timing.
 */

#include "modelStateTracker.h"

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

namespace {

// Probe callable returning a fixed liveness for any id.
auto constProbe(WorkerLiveness liveness)
{
    return [liveness](const std::string &) { return liveness; };
}

// gatherStats callable returning a loaded ModelInfo naming the id.
auto statsFor()
{
    return [](const std::string &id) {
        ModelInfo info;
        info.isServerRunning = true;
        info.isModelLoaded = true;
        info.loadedModel = id;
        return info;
    };
}

ModelLifecycle lifecycleOf(const ModelStateTracker &t, const std::string &id)
{
    auto snap = t.snapshot();
    auto it = snap.find(id);
    return it == snap.end() ? ModelLifecycle::GONE : it->second.lifecycle;
}

bool tracked(const ModelStateTracker &t, const std::string &id)
{
    return t.snapshot().count(id) > 0;
}

} // namespace

// ---------------------------------------------------------------------------
// Load lifecycle
// ---------------------------------------------------------------------------

TEST(ModelStateTracker, RequestLoad_ThenAlivePoll_BecomesLoaded)
{
    ModelStateTracker t;
    t.requestLoad("A");
    EXPECT_EQ(lifecycleOf(t, "A"), ModelLifecycle::LOADING);

    t.ingestPoll({"A"}, constProbe(WorkerLiveness::ALIVE), statsFor());
    EXPECT_EQ(lifecycleOf(t, "A"), ModelLifecycle::LOADED);
}

TEST(ModelStateTracker, LoadedPoll_PopulatesStats)
{
    ModelStateTracker t;
    t.requestLoad("A");
    t.ingestPoll({"A"}, constProbe(WorkerLiveness::ALIVE), statsFor());

    auto snap = t.snapshot();
    ASSERT_EQ(snap.count("A"), 1u);
    EXPECT_TRUE(snap["A"].stats.isModelLoaded);
    EXPECT_EQ(snap["A"].stats.loadedModel, "A");
}

// ---------------------------------------------------------------------------
// The central regression: intentional unload is never a crash
// ---------------------------------------------------------------------------

TEST(ModelStateTracker, RequestUnload_ThenAbsent_BecomesGone_NotCrashed)
{
    ModelStateTracker t;
    t.requestLoad("A");
    t.ingestPoll({"A"}, constProbe(WorkerLiveness::ALIVE), statsFor());
    ASSERT_EQ(lifecycleOf(t, "A"), ModelLifecycle::LOADED);

    // User unloads A; the server still lists it for a poll (lag).
    t.requestUnload("A");
    t.ingestPoll({"A"}, constProbe(WorkerLiveness::ALIVE), statsFor());
    EXPECT_EQ(lifecycleOf(t, "A"), ModelLifecycle::UNLOADING);

    // Next poll: A is gone. It must be a clean disappearance, not a crash.
    t.ingestPoll({}, constProbe(WorkerLiveness::ALIVE), statsFor());
    EXPECT_FALSE(tracked(t, "A"));
    EXPECT_TRUE(t.takeCrashed().empty());
}

TEST(ModelStateTracker, UnloadOneOfTwo_OtherStaysLoaded_NoCrash)
{
    ModelStateTracker t;
    t.requestLoad("A");
    t.requestLoad("B");
    t.ingestPoll({"A", "B"}, constProbe(WorkerLiveness::ALIVE), statsFor());
    ASSERT_EQ(lifecycleOf(t, "A"), ModelLifecycle::LOADED);
    ASSERT_EQ(lifecycleOf(t, "B"), ModelLifecycle::LOADED);

    // Unload A only; B remains loaded server-side.
    t.requestUnload("A");
    t.ingestPoll({"B"}, constProbe(WorkerLiveness::ALIVE), statsFor());

    EXPECT_FALSE(tracked(t, "A"));            // A cleanly gone
    EXPECT_EQ(lifecycleOf(t, "B"), ModelLifecycle::LOADED); // B untouched
    EXPECT_TRUE(t.takeCrashed().empty());     // no crash recovery triggered
}

// ---------------------------------------------------------------------------
// Crash detection
// ---------------------------------------------------------------------------

TEST(ModelStateTracker, Loaded_ThenAbsent_NoUnload_BecomesCrashed)
{
    ModelStateTracker t;
    t.requestLoad("A");
    t.ingestPoll({"A"}, constProbe(WorkerLiveness::ALIVE), statsFor());

    // A vanishes from /models with no unload request -> crash.
    t.ingestPoll({}, constProbe(WorkerLiveness::ALIVE), statsFor());
    EXPECT_EQ(t.takeCrashed(), std::vector<std::string>{"A"});
}

TEST(ModelStateTracker, DeadProbeGate_ThreeConsecutive_Crashes)
{
    ModelStateTracker t;
    t.requestLoad("A");
    t.ingestPoll({"A"}, constProbe(WorkerLiveness::ALIVE), statsFor());

    // DEAD while still reported loaded: gated at three consecutive.
    t.ingestPoll({"A"}, constProbe(WorkerLiveness::DEAD), statsFor()); // 1
    EXPECT_TRUE(t.takeCrashed().empty());
    t.ingestPoll({"A"}, constProbe(WorkerLiveness::DEAD), statsFor()); // 2
    EXPECT_TRUE(t.takeCrashed().empty());
    t.ingestPoll({"A"}, constProbe(WorkerLiveness::DEAD), statsFor()); // 3
    EXPECT_EQ(t.takeCrashed(), std::vector<std::string>{"A"});
}

TEST(ModelStateTracker, BusyProbe_Neutral_DoesNotCrash)
{
    ModelStateTracker t;
    t.requestLoad("A");
    t.ingestPoll({"A"}, constProbe(WorkerLiveness::ALIVE), statsFor());

    for (int i = 0; i < 10; ++i)
        t.ingestPoll({"A"}, constProbe(WorkerLiveness::BUSY), statsFor());

    EXPECT_TRUE(t.takeCrashed().empty());
    EXPECT_EQ(lifecycleOf(t, "A"), ModelLifecycle::LOADED);
}

TEST(ModelStateTracker, DeadStreakInterruptedByAlive_DoesNotCrash)
{
    ModelStateTracker t;
    t.requestLoad("A");
    t.ingestPoll({"A"}, constProbe(WorkerLiveness::ALIVE), statsFor());

    t.ingestPoll({"A"}, constProbe(WorkerLiveness::DEAD), statsFor());  // 1
    t.ingestPoll({"A"}, constProbe(WorkerLiveness::DEAD), statsFor());  // 2
    t.ingestPoll({"A"}, constProbe(WorkerLiveness::ALIVE), statsFor()); // reset
    t.ingestPoll({"A"}, constProbe(WorkerLiveness::DEAD), statsFor());  // 1 again
    EXPECT_TRUE(t.takeCrashed().empty());
}

TEST(ModelStateTracker, UnloadingModel_DeadProbe_IsGoneNotCrashed)
{
    // A worker can die mid-unload; with UNLOADING intent that is still not a
    // crash.
    ModelStateTracker t;
    t.requestLoad("A");
    t.ingestPoll({"A"}, constProbe(WorkerLiveness::ALIVE), statsFor());
    t.requestUnload("A");

    for (int i = 0; i < ModelStateTracker::kProbeFailThreshold; ++i)
        t.ingestPoll({"A"}, constProbe(WorkerLiveness::DEAD), statsFor());

    EXPECT_TRUE(t.takeCrashed().empty());
    EXPECT_EQ(lifecycleOf(t, "A"), ModelLifecycle::GONE);
}

// ---------------------------------------------------------------------------
// Reload after gone / crashed re-arms detection
// ---------------------------------------------------------------------------

TEST(ModelStateTracker, Reload_AfterCrash_ClearsAndLoads)
{
    ModelStateTracker t;
    t.requestLoad("A");
    t.ingestPoll({"A"}, constProbe(WorkerLiveness::ALIVE), statsFor());
    t.ingestPoll({}, constProbe(WorkerLiveness::ALIVE), statsFor()); // crash
    ASSERT_FALSE(t.takeCrashed().empty());

    // Reloading A starts a clean lifecycle.
    t.requestLoad("A");
    EXPECT_EQ(lifecycleOf(t, "A"), ModelLifecycle::LOADING);
    t.ingestPoll({"A"}, constProbe(WorkerLiveness::ALIVE), statsFor());
    EXPECT_EQ(lifecycleOf(t, "A"), ModelLifecycle::LOADED);
    EXPECT_TRUE(t.takeCrashed().empty());
}

// ---------------------------------------------------------------------------
// takeCrashed drains once
// ---------------------------------------------------------------------------

TEST(ModelStateTracker, TakeCrashed_DrainsOnce)
{
    ModelStateTracker t;
    t.requestLoad("A");
    t.ingestPoll({"A"}, constProbe(WorkerLiveness::ALIVE), statsFor());
    t.ingestPoll({}, constProbe(WorkerLiveness::ALIVE), statsFor());

    EXPECT_EQ(t.takeCrashed().size(), 1u);
    EXPECT_TRUE(t.takeCrashed().empty()); // second call is empty
}

// ---------------------------------------------------------------------------
// Force-unload-all suppression (issue #71)
// ---------------------------------------------------------------------------

TEST(ModelStateTracker, RequestUnloadAll_SetsSkipQueries_AndClears)
{
    ModelStateTracker t;
    t.requestLoad("A");
    t.ingestPoll({"A"}, constProbe(WorkerLiveness::ALIVE), statsFor());
    EXPECT_FALSE(t.shouldSkipModelQueries());

    t.requestUnloadAll();
    EXPECT_TRUE(t.shouldSkipModelQueries());
    EXPECT_TRUE(t.snapshot().empty());

    // A later load resumes polling.
    t.requestLoad("B");
    EXPECT_FALSE(t.shouldSkipModelQueries());
}

TEST(ModelStateTracker, OnServerOffline_DropsAllModels)
{
    ModelStateTracker t;
    t.requestLoad("A");
    t.ingestPoll({"A"}, constProbe(WorkerLiveness::ALIVE), statsFor());
    ASSERT_FALSE(t.snapshot().empty());

    t.onServerOffline();
    EXPECT_TRUE(t.snapshot().empty());
}

// ---------------------------------------------------------------------------
// evaluateProbe (gate unit)
// ---------------------------------------------------------------------------

TEST(ModelStateTracker, EvaluateProbe_AliveResets)
{
    int c = 2;
    EXPECT_FALSE(ModelStateTracker::evaluateProbe(WorkerLiveness::ALIVE, c));
    EXPECT_EQ(c, 0);
}

TEST(ModelStateTracker, EvaluateProbe_DeadCrossesThreshold)
{
    int c = 0;
    EXPECT_FALSE(ModelStateTracker::evaluateProbe(WorkerLiveness::DEAD, c));
    EXPECT_FALSE(ModelStateTracker::evaluateProbe(WorkerLiveness::DEAD, c));
    EXPECT_TRUE(ModelStateTracker::evaluateProbe(WorkerLiveness::DEAD, c));
    EXPECT_EQ(c, 0); // reset after crash
}

// ---------------------------------------------------------------------------
// Phase 4a: ingestPoll must not hold the lock across the probe/gatherStats
// callables. A callable that re-enters the tracker (snapshot / requestUnload)
// would self-deadlock on a non-recursive mutex if the gather phase held it.
// The lockless gather phase makes such re-entry safe — this pins the regression.
// ---------------------------------------------------------------------------

TEST(ModelStateTracker, IngestPoll_CallablesReenterTracker_NoDeadlock)
{
    ModelStateTracker t;
    t.requestLoad("A");
    t.ingestPoll({"A"}, constProbe(WorkerLiveness::ALIVE), statsFor());

    // Both callables re-enter the tracker. Pre-fix this hung on m_mutex; now the
    // gather phase is lockless so the reentrant reads/intent complete.
    auto reentrantProbe = [&t](const std::string &) {
        (void)t.snapshot();          // reentrant read
        return WorkerLiveness::ALIVE;
    };
    auto reentrantStats = [&t](const std::string &id) {
        (void)t.snapshot();          // reentrant read
        ModelInfo info;
        info.isModelLoaded = true;
        info.loadedModel = id;
        return info;
    };

    t.ingestPoll({"A"}, reentrantProbe, reentrantStats);
    EXPECT_EQ(lifecycleOf(t, "A"), ModelLifecycle::LOADED);
}

TEST(ModelStateTracker, IngestPoll_InvokesEachCallableOncePerId)
{
    ModelStateTracker t;
    t.requestLoad("A");
    t.requestLoad("B");
    t.ingestPoll({"A", "B"}, constProbe(WorkerLiveness::ALIVE), statsFor());

    std::map<std::string, int> probeCalls;
    std::map<std::string, int> statsCalls;
    auto countingProbe = [&probeCalls](const std::string &id) {
        ++probeCalls[id];
        return WorkerLiveness::ALIVE;
    };
    auto countingStats = [&statsCalls](const std::string &id) {
        ++statsCalls[id];
        ModelInfo info;
        info.loadedModel = id;
        return info;
    };

    t.ingestPoll({"A", "B"}, countingProbe, countingStats);

    EXPECT_EQ(probeCalls["A"], 1);
    EXPECT_EQ(probeCalls["B"], 1);
    EXPECT_EQ(statsCalls["A"], 1); // ALIVE -> stats gathered once
    EXPECT_EQ(statsCalls["B"], 1);
}
