#include "batchStore.h"
#include "configManager.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

/**
 * @brief Test fixture for BatchStore.
 *
 * Points HOME at a temp directory so ConfigManager/BatchStore operate on
 * isolated files, then reloads the singleton from the empty dir before each
 * test. Restores HOME on teardown. Mirrors ModelsIniTest.
 */
class BatchStoreTest : public ::testing::Test
{
  protected:
	void SetUp() override
	{
		const char *h = std::getenv("HOME");
		m_originalHome = h ? h : "";

		m_tmpDir = fs::temp_directory_path() / "workbench_test_batches";
		fs::remove_all(m_tmpDir);
		fs::create_directories(m_tmpDir);
		setenv("HOME", m_tmpDir.string().c_str(), 1);

		m_configDir = m_tmpDir / ".workbench";
		fs::create_directories(m_configDir);

		// Reset singleton state to the (empty) temp dir.
		BatchStore::instance().load();
	}

	void TearDown() override
	{
		if (m_originalHome.empty())
			unsetenv("HOME");
		else
			setenv("HOME", m_originalHome.c_str(), 1);
		fs::remove_all(m_tmpDir);
	}

	std::string readFile() const
	{
		std::ifstream f(m_configDir / "batches.json");
		return std::string((std::istreambuf_iterator<char>(f)),
						   std::istreambuf_iterator<char>());
	}

	std::string m_originalHome;
	fs::path m_tmpDir;
	fs::path m_configDir;
};

TEST_F(BatchStoreTest, SaveBatch_RoundTrips)
{
	auto &store = BatchStore::instance();
	ASSERT_TRUE(store.saveBatch({ "smoke", { "preset-a", "preset-b" } }));

	// Reload from disk to prove persistence (not just in-memory state).
	ASSERT_TRUE(store.load());
	const auto got = store.getBatch("smoke");
	ASSERT_TRUE(got.has_value());
	EXPECT_EQ(got->name, "smoke");
	EXPECT_EQ(got->presets, (std::vector<std::string>{ "preset-a", "preset-b" }));
}

TEST_F(BatchStoreTest, GetBatch_Unknown_ReturnsNullopt)
{
	EXPECT_FALSE(BatchStore::instance().getBatch("nope").has_value());
}

TEST_F(BatchStoreTest, GetBatchNames_DistinctSorted)
{
	auto &store = BatchStore::instance();
	ASSERT_TRUE(store.saveBatch({ "zeta", { "p" } }));
	ASSERT_TRUE(store.saveBatch({ "alpha", { "p" } }));
	ASSERT_TRUE(store.saveBatch({ "mid", { "p" } }));

	EXPECT_EQ(store.getBatchNames(),
			  (std::vector<std::string>{ "alpha", "mid", "zeta" }));
}

TEST_F(BatchStoreTest, SaveBatch_EmptyName_Rejected)
{
	auto &store = BatchStore::instance();
	EXPECT_FALSE(store.saveBatch({ "", { "preset-a" } }));
	EXPECT_TRUE(store.getBatchNames().empty());
}

TEST_F(BatchStoreTest, SaveBatch_EmptyPresetList_Allowed)
{
	auto &store = BatchStore::instance();
	EXPECT_TRUE(store.saveBatch({ "empty", {} }));
	const auto got = store.getBatch("empty");
	ASSERT_TRUE(got.has_value());
	EXPECT_TRUE(got->presets.empty());
}

TEST_F(BatchStoreTest, DeleteBatch_RemovesIt)
{
	auto &store = BatchStore::instance();
	ASSERT_TRUE(store.saveBatch({ "smoke", { "preset-a" } }));
	ASSERT_TRUE(store.deleteBatch("smoke"));
	EXPECT_FALSE(store.getBatch("smoke").has_value());

	// Persisted: a fresh load (file still exists, now with no batches) no longer
	// sees it.
	ASSERT_TRUE(store.load());
	EXPECT_FALSE(store.getBatch("smoke").has_value());
	EXPECT_TRUE(store.getBatchNames().empty());
}

TEST_F(BatchStoreTest, DeleteBatch_Unknown_ReturnsFalse)
{
	EXPECT_FALSE(BatchStore::instance().deleteBatch("ghost"));
}

TEST_F(BatchStoreTest, Load_MissingFile_Empty)
{
	// No save happened; the file does not exist.
	auto &store = BatchStore::instance();
	EXPECT_FALSE(store.load());
	EXPECT_TRUE(store.getBatchNames().empty());
}

TEST_F(BatchStoreTest, SaveBatch_DuplicatePresetsPreserved)
{
	auto &store = BatchStore::instance();
	ASSERT_TRUE(store.saveBatch({ "dup", { "preset-a", "preset-a", "preset-b" } }));
	ASSERT_TRUE(store.load());
	const auto got = store.getBatch("dup");
	ASSERT_TRUE(got.has_value());
	EXPECT_EQ(got->presets,
			  (std::vector<std::string>{ "preset-a", "preset-a", "preset-b" }));
}

TEST_F(BatchStoreTest, Load_InvalidJson_EmptyStore)
{
	{
		std::ofstream f(m_configDir / "batches.json");
		f << "{ this is not valid json";
	}
	auto &store = BatchStore::instance();
	EXPECT_FALSE(store.load());
	EXPECT_TRUE(store.getBatchNames().empty());
}
