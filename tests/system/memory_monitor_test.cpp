#include <gtest/gtest.h>
#include "system/ramMonitor.h"
#include "models/memoryStats.h"
#include <thread>
#include <chrono>

// =============================================================================
// MemoryMonitor Tests
// =============================================================================

TEST(MemoryMonitor, GetStatsReturnsValidStructure) {
    auto& monitor = MemoryMonitor::instance();
    
    // Should return a valid MemoryStats with default values
    auto stats = monitor.getStats();
    
    // Basic sanity checks
    EXPECT_GE(stats.totalMb, 0u);
    EXPECT_GE(stats.usedMb, 0u);
    EXPECT_GE(stats.availableMb, 0u);
    EXPECT_GE(stats.usagePercentage, 0.0);
    EXPECT_LE(stats.usagePercentage, 100.0);
}

TEST(MemoryMonitor, TryUpdateReturnsValidStats) {
    auto& monitor = MemoryMonitor::instance();
    
    // tryUpdate should return a valid stats after update
    auto stats = monitor.tryUpdate();
    
    ASSERT_TRUE(stats.has_value());
    EXPECT_GE(stats->totalMb, 0u);
    EXPECT_GE(stats->usedMb, 0u);
    EXPECT_GE(stats->availableMb, 0u);
}

TEST(MemoryMonitor, UpdateDoesNotCrash) {
    auto& monitor = MemoryMonitor::instance();
    
    // Should not throw or crash
    EXPECT_NO_THROW(monitor.update());
    
    // Stats should still be valid after update
    auto stats = monitor.getStats();
    EXPECT_GE(stats.usagePercentage, 0.0);
    EXPECT_LE(stats.usagePercentage, 100.0);
}

TEST(MemoryMonitor, ConcurrentUpdateAndGetStats) {
    auto& monitor = MemoryMonitor::instance();
    
    std::atomic<bool> running{true};
    std::thread updater([&monitor, &running]() {
        while (running) {
            monitor.update();
        }
    });
    
    std::thread reader([&monitor, &running]() {
        while (running) {
            auto stats = monitor.getStats();
            // Just verify basic invariants
            EXPECT_GE(stats.totalMb, 0u);
            EXPECT_GE(stats.usagePercentage, 0.0);
            EXPECT_LE(stats.usagePercentage, 100.0);
        }
    });
    
    // Let them run for a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    running = false;
    
    updater.join();
    reader.join();
}

TEST(MemoryMonitor, SingletonReturnsSameInstance) {
    auto& instance1 = MemoryMonitor::instance();
    auto& instance2 = MemoryMonitor::instance();
    
    // Should be the same instance
    EXPECT_EQ(&instance1, &instance2);
}

TEST(MemoryMonitor, MultipleTryUpdatesReturnValidStats) {
    auto& monitor = MemoryMonitor::instance();
    
    // Multiple calls should all return valid stats
    for (int i = 0; i < 5; i++) {
        auto stats = monitor.tryUpdate();
        ASSERT_TRUE(stats.has_value());
        EXPECT_GE(stats->totalMb, 0u);
        EXPECT_GE(stats->usedMb, 0u);
    }
}

TEST(MemoryMonitor, UsedPlusAvailableEqualsTotal) {
    auto& monitor = MemoryMonitor::instance();
    
    auto stats = monitor.tryUpdate();
    ASSERT_TRUE(stats.has_value());
    
    // used + available should equal total (within small margin for rounding)
    uint64_t totalCheck = stats->usedMb + stats->availableMb;
    // Allow small difference due to rounding or overhead
    if (stats->totalMb > 0) {
        // This is a loose check due to rounding in /proc/meminfo parsing
        EXPECT_NEAR(static_cast<double>(totalCheck), static_cast<double>(stats->totalMb), 100.0);
    }
}
