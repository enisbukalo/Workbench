#include <gtest/gtest.h>
#include "system/gpuMonitor.h"
#include "models/memoryStats.h"
#include "models/processorStats.h"
#include <thread>
#include <chrono>

// =============================================================================
// GpuMonitor Tests
// Note: These tests handle the case where nvidia-smi is not available.
// In that case, getStats() and getLoadStats() return empty vectors.
// =============================================================================

TEST(GpuMonitor, GetStatsReturnsValidOrEmpty) {
    auto& monitor = GpuMonitor::instance();
    
    // Try to get stats - may be empty if no GPU
    auto stats = monitor.getStats();
    
    // If not empty, validate structure
    for (const auto& s : stats) {
        EXPECT_GE(s.id, 0);
        EXPECT_GE(s.totalMb, 0u);
        EXPECT_GE(s.usedMb, 0u);
        EXPECT_GE(s.availableMb, 0u);
        EXPECT_GE(s.usagePercentage, 0.0);
        EXPECT_LE(s.usagePercentage, 100.0);
    }
}

TEST(GpuMonitor, GetLoadStatsReturnsValidOrEmpty) {
    auto& monitor = GpuMonitor::instance();
    
    // Try to get load stats - may be empty if no GPU
    auto stats = monitor.getLoadStats();
    
    // If not empty, validate structure
    for (const auto& s : stats) {
        EXPECT_GE(s.usagePercentage, 0.0);
        EXPECT_LE(s.usagePercentage, 100.0);
        // Temperature is either the "unavailable" sentinel or a sane reading.
        EXPECT_TRUE(s.temperatureC == -1.0 ||
                    (s.temperatureC >= 0.0 && s.temperatureC < 150.0));
    }
}

TEST(GpuMonitor, TemperatureDefaultsToSentinel) {
    // A default-constructed snapshot must report "unavailable" temperature.
    ProcessorStats s{};
    EXPECT_EQ(s.temperatureC, -1.0);
}

TEST(GpuMonitor, UpdateDoesNotCrash) {
    auto& monitor = GpuMonitor::instance();
    
    // Should not throw or crash even if nvidia-smi is not available
    EXPECT_NO_THROW(monitor.update());
}

TEST(GpuMonitor, ConcurrentUpdateAndGetStats) {
    auto& monitor = GpuMonitor::instance();
    
    std::atomic<bool> running{true};
    std::thread updater([&monitor, &running]() {
        while (running) {
            monitor.update();
        }
    });
    
    std::thread reader([&monitor, &running]() {
        while (running) {
            auto stats = monitor.getStats();
            auto loadStats = monitor.getLoadStats();
            
            // If we have GPUs, verify basic invariants
            for (const auto& s : stats) {
                EXPECT_GE(s.usagePercentage, 0.0);
                EXPECT_LE(s.usagePercentage, 100.0);
            }
            for (const auto& s : loadStats) {
                EXPECT_GE(s.usagePercentage, 0.0);
                EXPECT_LE(s.usagePercentage, 100.0);
                EXPECT_TRUE(s.temperatureC == -1.0 ||
                            (s.temperatureC >= 0.0 && s.temperatureC < 150.0));
            }
        }
    });
    
    // Let them run for a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    running = false;
    
    updater.join();
    reader.join();
}

TEST(GpuMonitor, SingletonReturnsSameInstance) {
    auto& instance1 = GpuMonitor::instance();
    auto& instance2 = GpuMonitor::instance();
    
    // Should be the same instance
    EXPECT_EQ(&instance1, &instance2);
}

TEST(GpuMonitor, StatsAndLoadStatsVectorsHaveSameSize) {
    auto& monitor = GpuMonitor::instance();
    
    monitor.update();
    
    auto stats = monitor.getStats();
    auto loadStats = monitor.getLoadStats();
    
    // If we have GPUs, both vectors should have the same size
    if (!stats.empty()) {
        EXPECT_EQ(stats.size(), loadStats.size());
    }
}

TEST(GpuMonitor, MemoryUsageConsistency) {
    auto& monitor = GpuMonitor::instance();
    
    monitor.update();
    
    auto stats = monitor.getStats();
    
    // For each GPU, verify memory usage is consistent
    for (const auto& s : stats) {
        if (s.totalMb > 0) {
            // usagePercentage should be approximately (used / total) * 100
            double expectedUsage = (static_cast<double>(s.usedMb) / static_cast<double>(s.totalMb)) * 100.0;
            EXPECT_NEAR(s.usagePercentage, expectedUsage, 1.0);
        }
    }
}
