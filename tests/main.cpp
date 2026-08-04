/**
 * @file main.cpp
 * @brief Test entry point for Workbench unit tests.
 *
 * This file serves as the main entry point for Google Test.
 * It includes gtest and runs all registered tests.
 *
 * To add new tests:
 * 1. Create test files in appropriate subdirectories under tests/
 * 2. Add them to tests/CMakeLists.txt
 */

#include <gtest/gtest.h>

#include <spdlog/spdlog.h>

// Placeholder test to verify gtest is working
TEST(SanityCheck, GTestIsRunning)
{
    EXPECT_EQ(1 + 1, 2);
}

TEST(SanityCheck, BasicAssertions)
{
    EXPECT_TRUE(true);
    EXPECT_FALSE(false);
    EXPECT_EQ(42, 42);
    EXPECT_NE(1, 2);
    EXPECT_LT(1, 2);
    EXPECT_LE(1, 1);
    EXPECT_GT(2, 1);
    EXPECT_GE(1, 1);
}

int main(int argc, char **argv)
{
    // Silence application logging during tests. Code under test logs warnings
    // by design for branches the tests deliberately exercise (e.g. a models.ini
    // path set to a missing file), which floods test output without indicating
    // a failure. Keep only critical so a genuine crash is still visible.
    spdlog::set_level(spdlog::level::critical);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}