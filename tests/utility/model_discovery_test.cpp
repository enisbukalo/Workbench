#include <gtest/gtest.h>
#include "utility/modelDiscovery.h"
#include <filesystem>
#include <fstream>
#include <atomic>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;

// Helper to create temporary test directory with model files
class ModelDiscoveryTest : public ::testing::Test {
protected:
    fs::path testDir;
    
    void SetUp() override {
        testDir = fs::temp_directory_path() / "workbench_test_models";
        fs::create_directories(testDir);
    }
    
    void TearDown() override {
        fs::remove_all(testDir);
    }
    
    // Create a .gguf file in the test directory
    void createModelFile(const std::string& filename) {
        auto path = testDir / filename;
        std::ofstream ofs(path);
        ofs << "test model content";
        ofs.close();
    }
};

// =============================================================================
// ModelDiscovery Basic Tests
// =============================================================================

TEST_F(ModelDiscoveryTest, ScanDirectoryFindsFiles) {
    createModelFile("test1.gguf");
    createModelFile("test2.gguf");
    createModelFile("test3.gguf");
    
    auto& discovery = ModelDiscovery::instance();
    auto results = discovery.scanDirectory(testDir.string());
    
    EXPECT_EQ(results.size(), 3);
}

TEST_F(ModelDiscoveryTest, ScanDirectoryCaseInsensitive) {
    createModelFile("model1.GGUF");
    createModelFile("model2.gguf");
    createModelFile("model3.GgUf");
    
    auto& discovery = ModelDiscovery::instance();
    auto results = discovery.scanDirectory(testDir.string());
    
    EXPECT_EQ(results.size(), 3);
}

TEST_F(ModelDiscoveryTest, ScanDirectoryNonExistent) {
    auto& discovery = ModelDiscovery::instance();
    auto results = discovery.scanDirectory("/nonexistent/path/to/directory");
    
    EXPECT_TRUE(results.empty());
}

TEST_F(ModelDiscoveryTest, ScanDirectoryIgnoresNonGguf) {
    createModelFile("model1.gguf");
    createModelFile("model2.txt");
    createModelFile("model3.bin");
    createModelFile("model4.gguf");
    
    auto& discovery = ModelDiscovery::instance();
    auto results = discovery.scanDirectory(testDir.string());
    
    EXPECT_EQ(results.size(), 2);
}

TEST_F(ModelDiscoveryTest, ScanDirectoryRecursive) {
    fs::create_directories(testDir / "subdir1" / "nested");
    fs::create_directories(testDir / "subdir2");
    
    createModelFile("root.gguf");
    createModelFile("subdir1/model.gguf");
    createModelFile("subdir1/nested/deep.gguf");
    createModelFile("subdir2/another.gguf");
    
    auto& discovery = ModelDiscovery::instance();
    auto results = discovery.scanDirectory(testDir.string());
    
    EXPECT_EQ(results.size(), 4);
}

TEST_F(ModelDiscoveryTest, ScanDirectoryEmpty) {
    auto& discovery = ModelDiscovery::instance();
    auto results = discovery.scanDirectory(testDir.string());
    
    EXPECT_TRUE(results.empty());
}

// =============================================================================
// pathToDisplayName Tests
// =============================================================================

TEST(ModelDiscovery, PathToDisplayNameSimple) {
    auto& discovery = ModelDiscovery::instance();
    
    std::string result = discovery.pathToDisplayName("/path/to/model.gguf");
    EXPECT_EQ(result, "model");
}

TEST(ModelDiscovery, PathToDisplayNameWithQ) {
    auto& discovery = ModelDiscovery::instance();
    
    std::string result = discovery.pathToDisplayName("/models/Qwen3.5-27B.Q6_K.gguf");
    EXPECT_EQ(result, "Qwen3.5-27B.Q6_K");
}

TEST(ModelDiscovery, PathToDisplayNameMultipleDots) {
    auto& discovery = ModelDiscovery::instance();
    
    std::string result = discovery.pathToDisplayName("/path/Llama-3.1-8B-Instruct-Q5_K_M.gguf");
    EXPECT_EQ(result, "Llama-3.1-8B-Instruct-Q5_K_M");
}

TEST(ModelDiscovery, PathToDisplayNameNoPath) {
    auto& discovery = ModelDiscovery::instance();
    
    std::string result = discovery.pathToDisplayName("model.gguf");
    EXPECT_EQ(result, "model");
}

TEST(ModelDiscovery, PathToDisplayNameNoExtension) {
    auto& discovery = ModelDiscovery::instance();
    
    // Should still work even without .gguf
    std::string result = discovery.pathToDisplayName("/path/to/model");
    EXPECT_EQ(result, "model");
}

// =============================================================================
// Cache Tests
// Note: getCachedModels(), scanForModels(), and refreshCache() require 
// ConfigManager to be initialized with modelSearchPaths. These are integration
// tests, not unit tests. We test scanDirectory instead which is the core logic.
// =============================================================================

TEST_F(ModelDiscoveryTest, GetCachedModelsRequiresConfig) {
    // This test documents that getCachedModels() depends on ConfigManager
    // In unit tests without config setup, it returns empty
    auto& discovery = ModelDiscovery::instance();
    auto results = discovery.getCachedModels();
    
    // Without config, search paths are empty, so cache should be empty
    EXPECT_TRUE(results.empty());
}

// =============================================================================
// Integration Tests
// =============================================================================

TEST_F(ModelDiscoveryTest, FullWorkflow) {
    // Create test structure
    fs::create_directories(testDir / "models");
    createModelFile("models/llama.gguf");
    createModelFile("models/mistral.gguf");
    createModelFile("models/codellama.gguf");
    
    auto& discovery = ModelDiscovery::instance();
    
    // Scan
    auto models = discovery.scanDirectory(testDir.string());
    ASSERT_EQ(models.size(), 3);
    
    // Convert to display names
    std::vector<std::string> names;
    for (const auto& path : models) {
        names.push_back(discovery.pathToDisplayName(path));
    }
    
    // Should have all names
    EXPECT_EQ(names.size(), 3);
    EXPECT_TRUE(std::find(names.begin(), names.end(), "llama") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "mistral") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "codellama") != names.end());
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST(ModelDiscovery, PathToDisplayNameEmpty) {
    auto& discovery = ModelDiscovery::instance();
    
    // Should not crash
    std::string result = discovery.pathToDisplayName("");
    EXPECT_EQ(result, "");
}

TEST(ModelDiscovery, PathToDisplayNameOnlySlash) {
    auto& discovery = ModelDiscovery::instance();
    
    std::string result = discovery.pathToDisplayName("/");
    EXPECT_EQ(result, "");
}

TEST(ModelDiscovery, PathToDisplayNameLinuxPath) {
    auto& discovery = ModelDiscovery::instance();
    
    // Test Linux-style paths (what we actually use in the docker container)
    std::string result = discovery.pathToDisplayName("/models/Llama-2-7B.Q4_K_M.gguf");
    EXPECT_EQ(result, "Llama-2-7B.Q4_K_M");
}
