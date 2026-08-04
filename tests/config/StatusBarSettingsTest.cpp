/**
 * @file StatusBarSettingsTest.cpp
 * @brief Tests for StatusBarSettings serialization, validation, and loader.
 */

#include "statusBarSettings.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace Config;
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
/// RAII temp dir: created on construction, removed on destruction.
class TempDir
{
  public:
	TempDir()
	{
		m_path = fs::temp_directory_path() /
				 fs::path("wb_statusbar_test_" +
						  std::to_string(reinterpret_cast<std::uintptr_t>(this)));
		fs::create_directories(m_path);
	}
	~TempDir()
	{
		std::error_code ec;
		fs::remove_all(m_path, ec);
	}
	TempDir(const TempDir &) = delete;
	TempDir &operator=(const TempDir &) = delete;

	[[nodiscard]] std::string file(const std::string &name) const
	{
		return (m_path / name).string();
	}

  private:
	fs::path m_path;
};
} // namespace

// ============================================================================
// Serialization
// ============================================================================

TEST(StatusBarSettings, RoundTripPreservesValues)
{
	StatusBarSettings original;
	original.hintIntervalMs = 1234;
	original.hints = { "a", "b", "c" };
	original.keymaps = { { "Terminal", { "Ctrl+C: interrupt" } } };

	json j = original;
	auto restored = j.get<StatusBarSettings>();

	EXPECT_EQ(restored.hintIntervalMs, 1234);
	EXPECT_EQ(restored.hints, original.hints);
	ASSERT_TRUE(restored.keymaps.contains("Terminal"));
	EXPECT_EQ(restored.keymaps.at("Terminal"),
			  std::vector<std::string>{ "Ctrl+C: interrupt" });
}

TEST(StatusBarSettings, FromJsonMissingKeysFallsBackToDefaults)
{
	json j = json::object(); // empty object: no keys present
	auto s = j.get<StatusBarSettings>();

	StatusBarSettings defaults;
	EXPECT_EQ(s.hintIntervalMs, defaults.hintIntervalMs);
	EXPECT_TRUE(s.hints.empty());
	EXPECT_TRUE(s.keymaps.empty());
}

// ============================================================================
// validate()
// ============================================================================

TEST(StatusBarSettings, ValidateClampsIntervalToAtLeastOne)
{
	StatusBarSettings s;
	s.hintIntervalMs = 0;
	s.validate();
	EXPECT_EQ(s.hintIntervalMs, 1);

	s.hintIntervalMs = -50;
	s.validate();
	EXPECT_EQ(s.hintIntervalMs, 1);
}

TEST(StatusBarSettings, FromJsonClampsInvalidInterval)
{
	json j;
	j["hintIntervalMs"] = -5;
	auto s = j.get<StatusBarSettings>();
	EXPECT_EQ(s.hintIntervalMs, 1); // from_json calls validate()
}

// ============================================================================
// loadStatusBarSettings()
// ============================================================================

TEST(StatusBarSettings, LoadMissingFileReturnsDefaultsAndWritesFile)
{
	TempDir dir;
	const std::string path = dir.file("statusbar.json");
	ASSERT_FALSE(fs::exists(path));

	auto s = loadStatusBarSettings(path);

	// Defaults are non-empty (self-documenting seed).
	EXPECT_FALSE(s.hints.empty());
	EXPECT_GE(s.hintIntervalMs, 1);

	// Best-effort write created the file so the user can edit it.
	EXPECT_TRUE(fs::exists(path));
}

TEST(StatusBarSettings, LoadMalformedJsonReturnsDefaultsNoThrow)
{
	TempDir dir;
	const std::string path = dir.file("statusbar.json");
	{
		std::ofstream out(path);
		out << "{ this is not valid json ]";
	}

	StatusBarSettings s;
	EXPECT_NO_THROW({ s = loadStatusBarSettings(path); });
	EXPECT_FALSE(s.hints.empty()); // fell back to defaults
}

TEST(StatusBarSettings, LoadValidFileReturnsParsedValues)
{
	TempDir dir;
	const std::string path = dir.file("statusbar.json");
	{
		StatusBarSettings custom;
		custom.hintIntervalMs = 999;
		custom.hints = { "only-hint" };
		std::ofstream out(path);
		out << json(custom).dump(4);
	}

	auto s = loadStatusBarSettings(path);
	EXPECT_EQ(s.hintIntervalMs, 999);
	ASSERT_EQ(s.hints.size(), 1u);
	EXPECT_EQ(s.hints[0], "only-hint");
}
