/**
 * @file model_path_validation_test.cpp
 * @brief Unit tests for the cross-platform GGUF path validation helpers.
 */

#include "modelPathValidation.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

/**
 * Creates a unique temp directory for each test and cleans it up after, so
 * tests that touch the real filesystem stay isolated and leave no residue.
 */
class ModelPathValidationTest : public ::testing::Test
{
  protected:
	void SetUp() override
	{
		m_dir = fs::temp_directory_path() /
				("wb_pathvalid_" +
				 std::to_string(
					 reinterpret_cast<std::uintptr_t>(this)));
		fs::create_directories(m_dir);
	}

	void TearDown() override
	{
		std::error_code ec;
		fs::remove_all(m_dir, ec);
	}

	/** Create an empty file under the temp dir and return its full path. */
	std::string makeFile(const std::string &name) const
	{
		fs::path p = m_dir / name;
		std::ofstream(p).close();
		return p.string();
	}

	fs::path m_dir;
};

TEST_F(ModelPathValidationTest, ValidGgufPath_ReturnsEmpty)
{
	const std::string path = makeFile("model.gguf");
	EXPECT_EQ(pathvalid::validateGgufPath(path), "");
}

TEST_F(ModelPathValidationTest, UppercaseExtension_IsValid)
{
	const std::string path = makeFile("MODEL.GGUF");
	EXPECT_EQ(pathvalid::validateGgufPath(path), "");
}

TEST_F(ModelPathValidationTest, NonExistentPath_ReturnsError)
{
	const std::string path = (m_dir / "missing.gguf").string();
	EXPECT_FALSE(pathvalid::validateGgufPath(path).empty());
}

TEST_F(ModelPathValidationTest, WrongExtension_ReturnsError)
{
	const std::string path = makeFile("model.bin");
	EXPECT_FALSE(pathvalid::validateGgufPath(path).empty());
}

TEST_F(ModelPathValidationTest, DirectoryPath_ReturnsError)
{
	EXPECT_FALSE(pathvalid::validateGgufPath(m_dir.string()).empty());
}

TEST_F(ModelPathValidationTest, EmptyPath_ReturnsError)
{
	EXPECT_FALSE(pathvalid::validateGgufPath("").empty());
	EXPECT_FALSE(pathvalid::validateGgufPath("   ").empty());
}

TEST_F(ModelPathValidationTest, QuotedAndPaddedPath_IsAccepted)
{
	const std::string path = makeFile("model.gguf");
	EXPECT_EQ(pathvalid::validateGgufPath("  \"" + path + "\"  "), "");
	EXPECT_EQ(pathvalid::validateGgufPath("'" + path + "'"), "");
}

TEST(ModelPathValidationDeriveName, StripsDirAndExtension)
{
	EXPECT_EQ(pathvalid::deriveModelName("/a/b/Mistral-7B.gguf"), "Mistral-7B");
}

TEST(ModelPathValidationDeriveName, UppercaseExtensionStripped)
{
	EXPECT_EQ(pathvalid::deriveModelName("/a/b/Model.GGUF"), "Model");
}

TEST(ModelPathValidationDeriveName, HandlesQuotedPath)
{
	EXPECT_EQ(pathvalid::deriveModelName("\"/a/b/Qwen3.gguf\""), "Qwen3");
}

TEST(ModelPathValidationDeriveName, EmptyInput_ReturnsEmpty)
{
	EXPECT_EQ(pathvalid::deriveModelName(""), "");
}

TEST(ModelPathValidationClean, TrimsWhitespaceAndQuotes)
{
	EXPECT_EQ(pathvalid::cleanPath("  /x/y.gguf  "), "/x/y.gguf");
	EXPECT_EQ(pathvalid::cleanPath("\"/x/y.gguf\""), "/x/y.gguf");
	EXPECT_EQ(pathvalid::cleanPath("'/x/y.gguf'"), "/x/y.gguf");
}
