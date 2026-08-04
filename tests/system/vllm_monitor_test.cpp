#include <gtest/gtest.h>
#include "system/vllmMonitor.h"

// =============================================================================
// VllmMonitor::parseModelsResponse Tests
// =============================================================================

TEST(VllmMonitor, ParseModelsResponseEmptyOnEmptyBody)
{
	EXPECT_EQ(VllmMonitor::parseModelsResponse(""), "");
}

TEST(VllmMonitor, ParseModelsResponseReturnsFirstModelId)
{
	std::string response =
		R"({"object":"list","data":[{"id":"Qwen3-8B","object":"model","owned_by":"vllm"}]})";
	EXPECT_EQ(VllmMonitor::parseModelsResponse(response), "Qwen3-8B");
}

TEST(VllmMonitor, ParseModelsResponseReturnsFirstOfMany)
{
	std::string response =
		R"({"object":"list","data":[)"
		R"({"id":"modelA","object":"model"},)"
		R"({"id":"modelB","object":"model"}]})";
	EXPECT_EQ(VllmMonitor::parseModelsResponse(response), "modelA");
}

TEST(VllmMonitor, ParseModelsResponseEmptyOnMalformedBody)
{
	EXPECT_EQ(VllmMonitor::parseModelsResponse("not json"), "");
	EXPECT_EQ(VllmMonitor::parseModelsResponse(R"({"data":"oops"})"), "");
	EXPECT_EQ(VllmMonitor::parseModelsResponse(R"({"data":[]})"), "");
}

// =============================================================================
// VllmMonitor::parseMetricsResponse Tests
// =============================================================================

TEST(VllmMonitor, ParseMetricsResponseZeroOnEmptyInput)
{
	auto info = VllmMonitor::parseMetricsResponse("");
	EXPECT_DOUBLE_EQ(info.generationTokensPerSec, 0.0);
	EXPECT_DOUBLE_EQ(info.processingTokensPerSec, 0.0);
	EXPECT_EQ(info.totalPromptTokens, 0u);
	EXPECT_EQ(info.totalGenerationTokens, 0u);
	EXPECT_EQ(info.activeRequestCount, 0);
}

TEST(VllmMonitor, ParseMetricsResponseSkipsComments)
{
	std::string response =
		"# HELP vllm:prompt_tokens_total total prompt tokens\n"
		"# TYPE vllm:prompt_tokens_total counter\n";
	auto info = VllmMonitor::parseMetricsResponse(response);
	EXPECT_EQ(info.totalPromptTokens, 0u);
}

TEST(VllmMonitor, ParseMetricsResponsePromptTokensTotal)
{
	std::string response =
		"vllm:prompt_tokens_total{model_name=\"test\"} 98986\n";
	auto info = VllmMonitor::parseMetricsResponse(response);
	EXPECT_EQ(info.totalPromptTokens, 98986u);
}

TEST(VllmMonitor, ParseMetricsResponseGenerationTokensTotal)
{
	std::string response =
		"vllm:generation_tokens_total{model_name=\"test\"} 6829\n";
	auto info = VllmMonitor::parseMetricsResponse(response);
	EXPECT_EQ(info.totalGenerationTokens, 6829u);
}

TEST(VllmMonitor, ParseMetricsResponseNumRequestsRunning)
{
	std::string response =
		"vllm:num_requests_running{model_name=\"test\"} 3\n";
	auto info = VllmMonitor::parseMetricsResponse(response);
	EXPECT_EQ(info.activeRequestCount, 3);
}

TEST(VllmMonitor, ParseMetricsResponseAllFieldsFromRealResponse)
{
	std::string response =
		"# HELP vllm:prompt_tokens_total total prompt tokens\n"
		"# TYPE vllm:prompt_tokens_total counter\n"
		"vllm:prompt_tokens_total{model_name=\"Qwen\"} 98986\n"
		"# HELP vllm:generation_tokens_total total generated tokens\n"
		"# TYPE vllm:generation_tokens_total counter\n"
		"vllm:generation_tokens_total{model_name=\"Qwen\"} 6829\n"
		"# HELP vllm:num_requests_running running requests\n"
		"# TYPE vllm:num_requests_running gauge\n"
		"vllm:num_requests_running{model_name=\"Qwen\"} 2\n"
		"# HELP vllm:num_requests_waiting waiting requests\n"
		"# TYPE vllm:num_requests_waiting gauge\n"
		"vllm:num_requests_waiting{model_name=\"Qwen\"} 1\n"
		"# Unrelated metric should be ignored\n"
		"vllm:time_to_first_token_seconds{model_name=\"Qwen\"} 0.5\n";

	auto info = VllmMonitor::parseMetricsResponse(response);
	EXPECT_EQ(info.totalPromptTokens, 98986u);
	EXPECT_EQ(info.totalGenerationTokens, 6829u);
	EXPECT_EQ(info.activeRequestCount, 2);
}

TEST(VllmMonitor, ParseMetricsResponseStripsLabelDimensions)
{
	// Label dimensions are stripped before matching metric name
	std::string response =
		"vllm:prompt_tokens_total{model_name=\"my-model\",gpu_uuid=\"GPU-0\"} 50000\n"
		"vllm:generation_tokens_total{model_name=\"my-model\",gpu_uuid=\"GPU-0\"} 3000\n";
	auto info = VllmMonitor::parseMetricsResponse(response);
	EXPECT_EQ(info.totalPromptTokens, 50000u);
	EXPECT_EQ(info.totalGenerationTokens, 3000u);
}

TEST(VllmMonitor, ParseMetricsResponseNoLabels)
{
	// Metrics without label dimensions should also parse
	std::string response =
		"vllm:prompt_tokens_total 12345\n"
		"vllm:generation_tokens_total 6789\n"
		"vllm:num_requests_running 1\n";
	auto info = VllmMonitor::parseMetricsResponse(response);
	EXPECT_EQ(info.totalPromptTokens, 12345u);
	EXPECT_EQ(info.totalGenerationTokens, 6789u);
	EXPECT_EQ(info.activeRequestCount, 1);
}

TEST(VllmMonitor, ParseMetricsResponseIgnoresUnprefixedNames)
{
	// Old broken metric names (without vllm: prefix) must not match
	std::string response =
		"prompt_tokens_total{model_name=\"test\"} 5000\n"
		"generation_tokens_total{model_name=\"test\"} 1000\n"
		"num_requests_running{model_name=\"test\"} 99\n";
	auto info = VllmMonitor::parseMetricsResponse(response);
	EXPECT_EQ(info.totalPromptTokens, 0u);
	EXPECT_EQ(info.totalGenerationTokens, 0u);
	EXPECT_EQ(info.activeRequestCount, 0);
}

TEST(VllmMonitor, ParseMetricsResponseIgnoresMalformedLines)
{
	std::string response =
		"vllm:prompt_tokens_total\n"
		"vllm:generation_tokens_total not_a_number\n"
		"vllm:prompt_tokens_total 500\n";
	auto info = VllmMonitor::parseMetricsResponse(response);
	EXPECT_EQ(info.totalPromptTokens, 500u);
	EXPECT_EQ(info.totalGenerationTokens, 0u);
}

// =============================================================================
// VllmThroughputTracker Tests
// =============================================================================

TEST(VllmThroughputTracker, EstablishesBaselineWithoutLifetimeCounterSpike)
{
	VllmThroughputTracker tracker;
	ModelInfo counters{};
	counters.totalPromptTokens = 500000;
	counters.totalGenerationTokens = 10000;

	auto result = tracker.update(counters, VllmThroughputTracker::Clock::time_point{});

	EXPECT_DOUBLE_EQ(result.processingTokensPerSec, 0.0);
	EXPECT_DOUBLE_EQ(result.generationTokensPerSec, 0.0);
}

TEST(VllmThroughputTracker, RetainsFinalRunningAveragesWhileIdle)
{
	using namespace std::chrono_literals;
	const auto start = VllmThroughputTracker::Clock::time_point{};
	VllmThroughputTracker tracker;
	ModelInfo counters{};
	counters.totalPromptTokens = 1000;
	counters.totalGenerationTokens = 100;
	tracker.update(counters, start);

	counters.activeRequestCount = 1;
	counters.totalPromptTokens = 1200;
	auto prefill = tracker.update(counters, start + 1s);
	EXPECT_DOUBLE_EQ(prefill.processingTokensPerSec, 200.0);
	EXPECT_DOUBLE_EQ(prefill.generationTokensPerSec, 0.0);
	EXPECT_EQ(prefill.activityState, ActivityState::PROMPT);

	counters.totalGenerationTokens = 140;
	auto generating = tracker.update(counters, start + 2s);
	EXPECT_DOUBLE_EQ(generating.processingTokensPerSec, 200.0);
	EXPECT_DOUBLE_EQ(generating.generationTokensPerSec, 40.0);
	EXPECT_EQ(generating.activityState, ActivityState::GENERATING);

	counters.activeRequestCount = 0;
	counters.totalGenerationTokens = 180;
	auto completed = tracker.update(counters, start + 3s);
	EXPECT_DOUBLE_EQ(completed.processingTokensPerSec, 200.0);
	EXPECT_DOUBLE_EQ(completed.generationTokensPerSec, 40.0);
	EXPECT_EQ(completed.activityState, ActivityState::IDLE);

	auto retained = tracker.update(counters, start + 10s);
	EXPECT_DOUBLE_EQ(retained.processingTokensPerSec, 200.0);
	EXPECT_DOUBLE_EQ(retained.generationTokensPerSec, 40.0);
}

TEST(VllmThroughputTracker, LaterRequestsContributeWithoutResettingAverages)
{
	using namespace std::chrono_literals;
	const auto start = VllmThroughputTracker::Clock::time_point{};
	VllmThroughputTracker tracker;
	ModelInfo counters{};
	counters.totalPromptTokens = 1000;
	counters.totalGenerationTokens = 100;
	tracker.update(counters, start);

	counters.activeRequestCount = 1;
	counters.totalPromptTokens = 1200;
	tracker.update(counters, start + 1s);
	counters.totalGenerationTokens = 140;
	tracker.update(counters, start + 2s);
	counters.activeRequestCount = 0;
	counters.totalGenerationTokens = 180;
	auto firstCompleted = tracker.update(counters, start + 3s);
	ASSERT_DOUBLE_EQ(firstCompleted.processingTokensPerSec, 200.0);
	ASSERT_DOUBLE_EQ(firstCompleted.generationTokensPerSec, 40.0);

	tracker.update(counters, start + 10s);
	counters.activeRequestCount = 1;
	counters.totalPromptTokens = 1300;
	auto secondPrefill = tracker.update(counters, start + 11s);
	EXPECT_DOUBLE_EQ(secondPrefill.processingTokensPerSec, 150.0);
	EXPECT_DOUBLE_EQ(secondPrefill.generationTokensPerSec, 40.0);
	EXPECT_EQ(secondPrefill.activityState, ActivityState::PROMPT);

	counters.totalGenerationTokens = 200;
	auto secondGenerating = tracker.update(counters, start + 12s);
	EXPECT_DOUBLE_EQ(secondGenerating.processingTokensPerSec, 150.0);
	EXPECT_DOUBLE_EQ(secondGenerating.generationTokensPerSec, 100.0 / 3.0);
	EXPECT_EQ(secondGenerating.activityState, ActivityState::GENERATING);
}

TEST(VllmThroughputTracker, CounterRollbackStartsFreshBaseline)
{
	using namespace std::chrono_literals;
	const auto start = VllmThroughputTracker::Clock::time_point{};
	VllmThroughputTracker tracker;
	ModelInfo counters{};
	counters.totalPromptTokens = 1000;
	counters.totalGenerationTokens = 100;
	tracker.update(counters, start);

	counters.activeRequestCount = 1;
	counters.totalGenerationTokens = 150;
	EXPECT_DOUBLE_EQ(
		tracker.update(counters, start + 1s).generationTokensPerSec, 50.0);

	counters.totalPromptTokens = 10;
	counters.totalGenerationTokens = 2;
	auto reset = tracker.update(counters, start + 2s);
	EXPECT_DOUBLE_EQ(reset.processingTokensPerSec, 0.0);
	EXPECT_DOUBLE_EQ(reset.generationTokensPerSec, 0.0);
}

// =============================================================================
// VllmMonitor singleton — basic lifecycle
// =============================================================================

TEST(VllmMonitor, GetStatsReturnsDefaultWhenNotStarted)
{
	// Before start(), stats should be default-constructed (all zeros/false)
	auto info = VllmMonitor::instance().getStats();
	EXPECT_FALSE(info.isServerRunning);
	EXPECT_FALSE(info.isModelLoaded);
	EXPECT_TRUE(info.loadedModel.empty());
}

TEST(VllmMonitor, StartStopIsIdempotent)
{
	// Multiple starts should not create multiple threads
	VllmMonitor::instance().start();
	VllmMonitor::instance().start();
	VllmMonitor::instance().stop();
	VllmMonitor::instance().stop();
	// If not idempotent, this would hang or crash
}
