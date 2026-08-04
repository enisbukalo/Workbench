#include <gtest/gtest.h>
#include "system/cpuMonitor.h"
#include "models/processorStats.h"
#include <thread>
#include <chrono>

// =============================================================================
// CpuMonitor Tests
// =============================================================================

TEST(CpuMonitor, GetStatsReturnsValidStructure) {
    auto& monitor = CpuMonitor::instance();
    
    // Should return a valid ProcessorStats with default values
    auto stats = monitor.getStats();
    
    // usagePercentage should be in valid range [0.0, 100.0]
    EXPECT_GE(stats.usagePercentage, 0.0);
    EXPECT_LE(stats.usagePercentage, 100.0);

    // temperatureC is either the unavailable sentinel or a plausible reading.
    EXPECT_TRUE(stats.temperatureC == -1.0 ||
                (stats.temperatureC >= 0.0 && stats.temperatureC < 150.0));
}

TEST(CpuMonitor, TemperatureDefaultsToSentinel) {
    ProcessorStats stats{};
    EXPECT_EQ(stats.temperatureC, -1.0);
}

TEST(CpuMonitor, TryUpdateReturnsValidStats) {
    auto& monitor = CpuMonitor::instance();
    
    // tryUpdate should return a valid stats after update
    auto stats = monitor.tryUpdate();
    
    ASSERT_TRUE(stats.has_value());
    EXPECT_GE(stats->usagePercentage, 0.0);
    EXPECT_LE(stats->usagePercentage, 100.0);
}

TEST(CpuMonitor, UpdateDoesNotCrash) {
    auto& monitor = CpuMonitor::instance();
    
    // Should not throw or crash
    EXPECT_NO_THROW(monitor.update());
    
    // Stats should still be valid after update
    auto stats = monitor.getStats();
    EXPECT_GE(stats.usagePercentage, 0.0);
    EXPECT_LE(stats.usagePercentage, 100.0);
}

TEST(CpuMonitor, ConcurrentUpdateAndGetStats) {
    auto& monitor = CpuMonitor::instance();
    
    std::atomic<bool> running{true};
    std::thread updater([&monitor, &running]() {
        while (running) {
            monitor.update();
        }
    });
    
    std::thread reader([&monitor, &running]() {
        while (running) {
            auto stats = monitor.getStats();
            // Just verify it's in valid range
            EXPECT_GE(stats.usagePercentage, 0.0);
            EXPECT_LE(stats.usagePercentage, 100.0);
            EXPECT_TRUE(stats.temperatureC == -1.0 ||
                        (stats.temperatureC >= 0.0 &&
                         stats.temperatureC < 150.0));
        }
    });
    
    // Let them run for a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    running = false;
    
    updater.join();
    reader.join();
}

TEST(CpuMonitor, SingletonReturnsSameInstance) {
    auto& instance1 = CpuMonitor::instance();
    auto& instance2 = CpuMonitor::instance();
    
    // Should be the same instance
    EXPECT_EQ(&instance1, &instance2);
}

TEST(CpuMonitor, MultipleTryUpdatesReturnValidStats) {
    auto& monitor = CpuMonitor::instance();
    
    // Multiple calls should all return valid stats
    for (int i = 0; i < 5; i++) {
        auto stats = monitor.tryUpdate();
        ASSERT_TRUE(stats.has_value());
        EXPECT_GE(stats->usagePercentage, 0.0);
        EXPECT_LE(stats->usagePercentage, 100.0);
    }
}
