#include <gtest/gtest.h>
#include "system/modelInfoMonitor.h"
#include "modelStateTracker.h"

// =============================================================================
// ModelInfoMonitor::parseMetricsResponse Tests
// =============================================================================

TEST(ModelInfoMonitor, ParseMetricsResponseZeroOnEmptyInput)
{
	auto info = ModelInfoMonitor::parseMetricsResponse("");
	EXPECT_DOUBLE_EQ(info.generationTokensPerSec, 0.0);
	EXPECT_DOUBLE_EQ(info.processingTokensPerSec, 0.0);
	EXPECT_EQ(info.totalPromptTokens, 0u);
	EXPECT_EQ(info.totalGenerationTokens, 0u);
}

TEST(ModelInfoMonitor, ParseMetricsResponseSkipsComments)
{
	std::string response =
		"# HELP llamacpp:predicted_tokens_seconds tokens/s\n"
		"# TYPE llamacpp:predicted_tokens_seconds gauge\n";
	auto info = ModelInfoMonitor::parseMetricsResponse(response);
	EXPECT_DOUBLE_EQ(info.generationTokensPerSec, 0.0);
}

TEST(ModelInfoMonitor, ParseMetricsResponseGenerationTokPerSec)
{
	std::string response =
		"llamacpp:predicted_tokens_seconds{model=\"test\"} 67.7964\n";
	auto info = ModelInfoMonitor::parseMetricsResponse(response);
	EXPECT_DOUBLE_EQ(info.generationTokensPerSec, 67.7964);
}

TEST(ModelInfoMonitor, ParseMetricsResponseProcessingTokPerSec)
{
	std::string response =
		"llamacpp:prompt_tokens_seconds{model=\"test\"} 1664.81\n";
	auto info = ModelInfoMonitor::parseMetricsResponse(response);
	EXPECT_DOUBLE_EQ(info.processingTokensPerSec, 1664.81);
}

TEST(ModelInfoMonitor, ParseMetricsResponseTotalPromptTokens)
{
	std::string response =
		"llamacpp:prompt_tokens_total{model=\"test\"} 98986\n";
	auto info = ModelInfoMonitor::parseMetricsResponse(response);
	EXPECT_EQ(info.totalPromptTokens, 98986u);
}

TEST(ModelInfoMonitor, ParseMetricsResponseTotalGenerationTokens)
{
	std::string response =
		"llamacpp:tokens_predicted_total{model=\"test\"} 6829\n";
	auto info = ModelInfoMonitor::parseMetricsResponse(response);
	EXPECT_EQ(info.totalGenerationTokens, 6829u);
}

TEST(ModelInfoMonitor, ParseMetricsResponseAllFieldsFromRealResponse)
{
	std::string response =
		"# HELP llamacpp:prompt_tokens_total total prompt tokens\n"
		"# TYPE llamacpp:prompt_tokens_total counter\n"
		"llamacpp:prompt_tokens_total{model=\"Qwen\"} 98986\n"
		"# HELP llamacpp:tokens_predicted_total total generated tokens\n"
		"# TYPE llamacpp:tokens_predicted_total counter\n"
		"llamacpp:tokens_predicted_total{model=\"Qwen\"} 6829\n"
		"# HELP llamacpp:prompt_tokens_seconds prompt throughput tok/s\n"
		"# TYPE llamacpp:prompt_tokens_seconds gauge\n"
		"llamacpp:prompt_tokens_seconds{model=\"Qwen\"} 1664.81\n"
		"# HELP llamacpp:predicted_tokens_seconds generation throughput tok/s\n"
		"# TYPE llamacpp:predicted_tokens_seconds gauge\n"
		"llamacpp:predicted_tokens_seconds{model=\"Qwen\"} 67.7964\n"
		"# Unrelated metric should be ignored\n"
		"llamacpp:tokens_predicted_seconds_total{model=\"Qwen\"} 100.728\n";

	auto info = ModelInfoMonitor::parseMetricsResponse(response);
	EXPECT_DOUBLE_EQ(info.totalPromptTokens, 98986u);
	EXPECT_DOUBLE_EQ(info.totalGenerationTokens, 6829u);
	EXPECT_DOUBLE_EQ(info.processingTokensPerSec, 1664.81);
	EXPECT_DOUBLE_EQ(info.generationTokensPerSec, 67.7964);
}

TEST(ModelInfoMonitor, ParseMetricsResponseIgnoresOldUnprefixedNames)
{
	// Old broken metric names (without llamacpp: prefix) must not match
	std::string response =
		"predicted_tokens_seconds{model=\"test\"} 99.9\n"
		"prompt_tokens_seconds{model=\"test\"} 888.0\n"
		"prompt_tokens_total{model=\"test\"} 5000\n"
		"tokens_predicted_total{model=\"test\"} 1000\n";
	auto info = ModelInfoMonitor::parseMetricsResponse(response);
	EXPECT_DOUBLE_EQ(info.generationTokensPerSec, 0.0);
	EXPECT_DOUBLE_EQ(info.processingTokensPerSec, 0.0);
	EXPECT_EQ(info.totalPromptTokens, 0u);
	EXPECT_EQ(info.totalGenerationTokens, 0u);
}

// =============================================================================
// ModelInfoMonitor::parseLoadedModelId Tests
//
// Drives the #85 fix: the poll loop re-queries /models every cycle and treats
// an empty id as "no model loaded", flipping the UI back to LOAD. These cover
// the body->id parsing contract that decision depends on.
// =============================================================================

TEST(ModelInfoMonitor, ParseLoadedModelIdEmptyOnEmptyBody)
{
	EXPECT_EQ(ModelInfoMonitor::parseLoadedModelId(""), "");
}

TEST(ModelInfoMonitor, ParseLoadedModelIdReturnsIdWhenLoaded)
{
	// A model whose status is "loaded": return its id.
	std::string response =
		R"({"data":[{"id":"Qwen3-8B","status":"loaded"}]})";
	EXPECT_EQ(ModelInfoMonitor::parseLoadedModelId(response), "Qwen3-8B");
}

TEST(ModelInfoMonitor, ParseLoadedModelIdEmptyWhenNoneLoaded)
{
	// Model present but unloaded server-side: no "loaded" status -> empty.
	// This is the #85 case where the UI must flip back to LOAD.
	std::string response =
		R"({"data":[{"id":"Qwen3-8B","status":"unloaded"}]})";
	EXPECT_EQ(ModelInfoMonitor::parseLoadedModelId(response), "");
}

TEST(ModelInfoMonitor, ParseLoadedModelIdEmptyOnMalformedBody)
{
	// "loaded" present but no preceding "id" field -> empty, not a crash.
	EXPECT_EQ(ModelInfoMonitor::parseLoadedModelId(R"({"status":"loaded"})"),
			  "");
	EXPECT_EQ(ModelInfoMonitor::parseLoadedModelId("not json at all"), "");
}

TEST(ModelInfoMonitor, ParseLoadedModelIdPicksLoadedAmongMany)
{
	// Multiple models, only the second is loaded: return its id, not the first.
	std::string response =
		R"({"data":[)"
		R"({"id":"modelA","status":"unloaded"},)"
		R"({"id":"modelB","status":"loaded"}]})";
	EXPECT_EQ(ModelInfoMonitor::parseLoadedModelId(response), "modelB");
}

// =============================================================================
// ModelInfoMonitor::parseLoadedModelIds Tests (multi-model foundation, #110)
//
// The poll loop iterates every "loaded" id and polls per model. These cover the
// body->ids contract that decision depends on.
// =============================================================================

TEST(ModelInfoMonitor, ParseLoadedModelIdsEmptyOnEmptyBody)
{
	EXPECT_TRUE(ModelInfoMonitor::parseLoadedModelIds("").empty());
}

TEST(ModelInfoMonitor, ParseLoadedModelIdsReturnsAllLoaded)
{
	std::string response =
		R"({"data":[)"
		R"({"id":"modelA","status":{"value":"loaded"}},)"
		R"({"id":"modelB","status":{"value":"loaded"}}]})";
	auto ids = ModelInfoMonitor::parseLoadedModelIds(response);
	ASSERT_EQ(ids.size(), 2u);
	EXPECT_EQ(ids[0], "modelA");
	EXPECT_EQ(ids[1], "modelB");
}

TEST(ModelInfoMonitor, ParseLoadedModelIdsSkipsUnloaded)
{
	std::string response =
		R"({"data":[)"
		R"({"id":"modelA","status":{"value":"unloaded"}},)"
		R"({"id":"modelB","status":{"value":"loaded"}}]})";
	auto ids = ModelInfoMonitor::parseLoadedModelIds(response);
	ASSERT_EQ(ids.size(), 1u);
	EXPECT_EQ(ids[0], "modelB");
}

TEST(ModelInfoMonitor, ParseLoadedModelIdsEmptyOnMalformedBody)
{
	EXPECT_TRUE(ModelInfoMonitor::parseLoadedModelIds("not json").empty());
	EXPECT_TRUE(
		ModelInfoMonitor::parseLoadedModelIds(R"({"data":"oops"})").empty());
}

// =============================================================================
// ModelInfoMonitor per-model stats accessors (#110)
//
// The monitor singleton has no injectable server seam, so these cover the
// query contract observable without a running server: an unknown id and the
// empty initial map.
// =============================================================================

TEST(ModelInfoMonitor, GetStatsForUnknownNameReturnsEmpty)
{
	auto info = ModelInfoMonitor::instance().getStatsFor("definitely-not-loaded");
	EXPECT_FALSE(info.isModelLoaded);
	EXPECT_TRUE(info.loadedModel.empty());
}

TEST(ModelInfoMonitor, UnloadAllClearsAllModels)
{
	// An unload-all through the tracker clears the published per-model map; with
	// no server loaded it is already empty, and must stay empty (no throw, no
	// stale entries). getAllStats() is a view over the tracker snapshot.
	ModelStateTracker::instance().requestUnloadAll();
	EXPECT_TRUE(ModelInfoMonitor::instance().getAllStats().empty());
}

// =============================================================================
// ModelInfoMonitor::evaluateProbeResult Tests (consecutive-DEAD crash gate)
//
// Distinguishes a real crash (DEAD = refused connection, #18912) from a busy
// worker (BUSY = slow router-proxied /slots). Only consecutive DEAD probes
// certify a crash; BUSY is neutral so a healthy-but-busy server is never
// restarted.
// =============================================================================

using WorkerState = LlamaServerProcess::WorkerState;

TEST(ModelInfoMonitor, EvaluateProbeResultAliveNeverCrashesAndResets)
{
	int deadCount = 2;
	EXPECT_FALSE(
		ModelInfoMonitor::evaluateProbeResult(WorkerState::ALIVE, deadCount));
	EXPECT_EQ(deadCount, 0);
}

TEST(ModelInfoMonitor, EvaluateProbeResultSingleBusyIsNeutral)
{
	// A busy worker (slow /slots) is reachable: it must NOT advance the streak.
	int deadCount = 0;
	EXPECT_FALSE(
		ModelInfoMonitor::evaluateProbeResult(WorkerState::BUSY, deadCount));
	EXPECT_EQ(deadCount, 0);
}

TEST(ModelInfoMonitor, EvaluateProbeResultRepeatedBusyNeverCrashes)
{
	int deadCount = 0;
	for (int i = 0; i < 10; ++i) {
		EXPECT_FALSE(ModelInfoMonitor::evaluateProbeResult(WorkerState::BUSY,
														   deadCount));
	}
	EXPECT_EQ(deadCount, 0);
}

TEST(ModelInfoMonitor, EvaluateProbeResultCrashesOnThirdConsecutiveDead)
{
	int deadCount = 0;
	EXPECT_FALSE(
		ModelInfoMonitor::evaluateProbeResult(WorkerState::DEAD, deadCount)); // 1
	EXPECT_FALSE(
		ModelInfoMonitor::evaluateProbeResult(WorkerState::DEAD, deadCount)); // 2
	EXPECT_TRUE(
		ModelInfoMonitor::evaluateProbeResult(WorkerState::DEAD, deadCount)); // 3
	// Reset after a declared crash so a later reload starts clean.
	EXPECT_EQ(deadCount, 0);
}

TEST(ModelInfoMonitor, EvaluateProbeResultBusyInterleavedInDeadStreakIsNeutral)
{
	// A BUSY probe in the middle of a DEAD streak neither advances nor resets:
	// the two surrounding DEADs still accumulate toward the threshold.
	int deadCount = 0;
	ModelInfoMonitor::evaluateProbeResult(WorkerState::DEAD, deadCount); // 1
	ModelInfoMonitor::evaluateProbeResult(WorkerState::BUSY, deadCount); // 1
	EXPECT_EQ(deadCount, 1);
	ModelInfoMonitor::evaluateProbeResult(WorkerState::DEAD, deadCount); // 2
	EXPECT_TRUE(
		ModelInfoMonitor::evaluateProbeResult(WorkerState::DEAD, deadCount)); // 3
	EXPECT_EQ(deadCount, 0);
}

TEST(ModelInfoMonitor, EvaluateProbeResultAliveResetsDeadStreak)
{
	// Two DEADs then an ALIVE must clear the streak, so a single later DEAD
	// does not tip over the threshold.
	int deadCount = 0;
	ModelInfoMonitor::evaluateProbeResult(WorkerState::DEAD, deadCount); // 1
	ModelInfoMonitor::evaluateProbeResult(WorkerState::DEAD, deadCount); // 2
	EXPECT_FALSE(
		ModelInfoMonitor::evaluateProbeResult(WorkerState::ALIVE, deadCount));
	EXPECT_EQ(deadCount, 0);
	EXPECT_FALSE(
		ModelInfoMonitor::evaluateProbeResult(WorkerState::DEAD, deadCount));
	EXPECT_EQ(deadCount, 1);
}

// =============================================================================
// ModelInfoMonitor::maxDecodedTokens Tests
//
// Drives the Generating-vs-Processing panel state: a slot with n_decoded > 0
// has emitted tokens (generating); n_decoded == 0 while processing is prompt
// prefill.
// =============================================================================

TEST(ModelInfoMonitor, MaxDecodedTokensZeroOnMissingKey)
{
	EXPECT_EQ(ModelInfoMonitor::maxDecodedTokens(""), 0);
	EXPECT_EQ(ModelInfoMonitor::maxDecodedTokens(R"([{"id":0}])"), 0);
}

TEST(ModelInfoMonitor, MaxDecodedTokensSingleSlot)
{
	std::string slots = R"([{"id":0,"n_decoded":42,"is_processing":true}])";
	EXPECT_EQ(ModelInfoMonitor::maxDecodedTokens(slots), 42);
}

TEST(ModelInfoMonitor, MaxDecodedTokensPicksMaxAcrossSlots)
{
	std::string slots =
		R"([{"id":0,"n_decoded":7},{"id":1,"n_decoded":128},)"
		R"({"id":2,"n_decoded":3}])";
	EXPECT_EQ(ModelInfoMonitor::maxDecodedTokens(slots), 128);
}

TEST(ModelInfoMonitor, MaxDecodedTokensZeroWhenAllZero)
{
	// Prefill: processing but no tokens decoded yet -> 0 (panel shows Processing)
	std::string slots =
		R"([{"id":0,"n_decoded":0,"is_processing":true}])";
	EXPECT_EQ(ModelInfoMonitor::maxDecodedTokens(slots), 0);
}

TEST(ModelInfoMonitor, MaxDecodedTokensIgnoresNonNumeric)
{
	// A non-numeric value must be skipped, not crash, and not count.
	std::string slots = R"([{"n_decoded":null},{"n_decoded":15}])";
	EXPECT_EQ(ModelInfoMonitor::maxDecodedTokens(slots), 15);
}
