#include <gtest/gtest.h>

#include "system/temperatureAverager.h"

#include <chrono>

// =============================================================================
// TemperatureAverager Tests
//
// Time is driven explicitly via the TimePoint overload of add() so the rolling
// window can be exercised deterministically without sleeping.
// =============================================================================

using std::chrono::seconds;
using TimePoint = TemperatureAverager::TimePoint;

namespace {
constexpr double UNAVAILABLE = TemperatureAverager::UNAVAILABLE;
}

TEST(TemperatureAverager, SingleSampleReturnsThatSample) {
	TemperatureAverager avg(seconds(60));
	const TimePoint t0;
	EXPECT_DOUBLE_EQ(avg.add(50.0, t0), 50.0);
	EXPECT_EQ(avg.sampleCount(), 1u);
}

TEST(TemperatureAverager, AveragesMultipleSamplesInWindow) {
	TemperatureAverager avg(seconds(60));
	const TimePoint t0;
	avg.add(40.0, t0);
	avg.add(60.0, t0 + seconds(1));
	// Mean of 40 and 60 is 50.
	EXPECT_DOUBLE_EQ(avg.add(50.0, t0 + seconds(2)), 50.0);
	EXPECT_EQ(avg.sampleCount(), 3u);
}

TEST(TemperatureAverager, EvictsSamplesOlderThanWindow) {
	TemperatureAverager avg(seconds(60));
	const TimePoint t0;
	avg.add(100.0, t0); // will fall out of the window
	// 61 s later, the first sample is older than the 60 s window and is dropped.
	EXPECT_DOUBLE_EQ(avg.add(20.0, t0 + seconds(61)), 20.0);
	EXPECT_EQ(avg.sampleCount(), 1u);
}

TEST(TemperatureAverager, PartialEvictionKeepsRecentSamples) {
	TemperatureAverager avg(seconds(60));
	const TimePoint t0;
	avg.add(10.0, t0);              // dropped at t0+61
	avg.add(30.0, t0 + seconds(40)); // still inside window at t0+61
	// At t0+61: t0 sample evicted, mean of 30 and 50 is 40.
	EXPECT_DOUBLE_EQ(avg.add(50.0, t0 + seconds(61)), 40.0);
	EXPECT_EQ(avg.sampleCount(), 2u);
}

TEST(TemperatureAverager, SampleExactlyAtWindowEdgeIsRetained) {
	TemperatureAverager avg(seconds(60));
	const TimePoint t0;
	avg.add(80.0, t0);
	// Cutoff is now - window; a sample whose age equals the window is NOT older
	// than the cutoff, so it is retained.
	EXPECT_DOUBLE_EQ(avg.add(80.0, t0 + seconds(60)), 80.0);
	EXPECT_EQ(avg.sampleCount(), 2u);
}

TEST(TemperatureAverager, UnavailableSampleReturnsSentinelAndIsNotStored) {
	TemperatureAverager avg(seconds(60));
	const TimePoint t0;
	avg.add(70.0, t0);
	// A sentinel reading must not be stored nor poison the average.
	EXPECT_DOUBLE_EQ(avg.add(UNAVAILABLE, t0 + seconds(1)), UNAVAILABLE);
	EXPECT_EQ(avg.sampleCount(), 1u);
	// The next valid reading still averages only the stored valid samples.
	EXPECT_DOUBLE_EQ(avg.add(90.0, t0 + seconds(2)), 80.0);
}

TEST(TemperatureAverager, AllSamplesEvictedReturnsLatestOnly) {
	TemperatureAverager avg(seconds(60));
	const TimePoint t0;
	avg.add(45.0, t0);
	// Far enough in the future that every prior sample is gone; only the new
	// sample remains, so the average equals it.
	EXPECT_DOUBLE_EQ(avg.add(65.0, t0 + seconds(600)), 65.0);
	EXPECT_EQ(avg.sampleCount(), 1u);
}

TEST(TemperatureAverager, RespectsCustomWindow) {
	TemperatureAverager avg(seconds(5));
	const TimePoint t0;
	avg.add(10.0, t0);
	// Beyond the 5 s custom window, the first sample is evicted.
	EXPECT_DOUBLE_EQ(avg.add(20.0, t0 + seconds(6)), 20.0);
	EXPECT_EQ(avg.sampleCount(), 1u);
}
