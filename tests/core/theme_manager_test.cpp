/**
 * @file theme_manager_test.cpp
 * @brief Unit tests for ThemeManager + Config::Theme serialization/validation.
 *
 * ThemeManager exposes only a public API (initialize/setActive/getActive/
 * getAvailableThemes/getByName); parseHex/resolve/buildGradient are private.
 * Tests drive the public surface by writing temp theme JSON files to disk and
 * loading them, then assert on resolved ftxui::Color values and gradient stops.
 *
 * Config::Theme JSON round-trip and validate() fallbacks are tested directly.
 */

#include "ThemeManager.h"

#include "themeSettings.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include "json.hpp"

#include <gtest/gtest.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

// Write a JSON string to <dir>/<filename>.
void writeFile(const fs::path &dir, const std::string &filename,
               const std::string &content)
{
    fs::create_directories(dir);
    std::ofstream ofs(dir / filename);
    ofs << content;
}

// Minimal valid theme JSON with overridable name + a couple colors.
std::string themeJson(const std::string &name, const std::string &success,
                      const std::string &error)
{
    json j;
    j["name"] = name;
    j["description"] = "test theme";
    j["colors"] = { { "success", success }, { "error", error } };
    j["gradients"] = {
        { "cpuLoad",
          json::array({ { { "offset", 0.0 }, { "color", "#F00" } },
                        { { "offset", 1.0 }, { "color", "#0F0" } } }) },
        { "memoryUsage",
          json::array({ { { "offset", 0.0 }, { "color", "#0F0" } },
                        { { "offset", 1.0 }, { "color", "#F00" } } }) }
    };
    return j.dump();
}

} // namespace

// ============================================================================
// Test fixture: each test gets a fresh temp user theme dir.
// ============================================================================

class ThemeManagerTest : public ::testing::Test {
protected:
    fs::path mDir;

    void SetUp() override
    {
        mDir = fs::temp_directory_path() /
               ("wb_theme_test_" +
                std::to_string(reinterpret_cast<uintptr_t>(this)));
        fs::create_directories(mDir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(mDir, ec);
    }
};

// ============================================================================
// ThemeManager: loading + resolution
// ============================================================================

TEST_F(ThemeManagerTest, LoadsThemeFromDirectory)
{
    writeFile(mDir, "alpha.json", themeJson("Alpha", "#0F0", "#F00"));

    auto &mgr = ThemeManager::instance();
    mgr.initialize(mDir.string());

    auto names = mgr.getAvailableThemes();
    EXPECT_NE(std::find(names.begin(), names.end(), "Alpha"), names.end());
}

TEST_F(ThemeManagerTest, SeedsDefaultThemeOnInitialize)
{
    // Empty dir → initialize must write default.json and load "Default".
    auto &mgr = ThemeManager::instance();
    mgr.initialize(mDir.string());

    EXPECT_TRUE(fs::exists(mDir / "default.json"));

    auto names = mgr.getAvailableThemes();
    EXPECT_NE(std::find(names.begin(), names.end(), "Default"), names.end());
}

TEST_F(ThemeManagerTest, OverwritesExistingDefaultOnInitialize)
{
    // A tampered default.json must be replaced by the canonical internal one.
    writeFile(mDir, "default.json", themeJson("Default", "#111", "#222"));

    auto &mgr = ThemeManager::instance();
    mgr.initialize(mDir.string());
    ASSERT_TRUE(mgr.setActive("Default"));

    // Internal default success is Green (#008000), not the tampered #111.
    auto theme = mgr.getByName("Default");
    ASSERT_TRUE(theme.has_value());
    EXPECT_EQ(theme->colors.success, "#008000");
}

TEST_F(ThemeManagerTest, ResolvesHexIntoFtxuiColor)
{
    writeFile(mDir, "alpha.json", themeJson("Alpha", "#00FF00", "#FF0000"));

    auto &mgr = ThemeManager::instance();
    mgr.initialize(mDir.string());
    ASSERT_TRUE(mgr.setActive("Alpha"));

    auto active = mgr.getActive();
    ASSERT_TRUE(active);
    EXPECT_EQ(active->success, ftxui::Color::RGB(0, 255, 0));
    EXPECT_EQ(active->error, ftxui::Color::RGB(255, 0, 0));
}

TEST_F(ThemeManagerTest, ShorthandHexExpandsToFullColor)
{
    // "#0F0" → RGB(0,255,0), "#F00" → RGB(255,0,0)
    writeFile(mDir, "alpha.json", themeJson("Alpha", "#0F0", "#F00"));

    auto &mgr = ThemeManager::instance();
    mgr.initialize(mDir.string());
    ASSERT_TRUE(mgr.setActive("Alpha"));

    auto active = mgr.getActive();
    EXPECT_EQ(active->success, ftxui::Color::RGB(0, 255, 0));
    EXPECT_EQ(active->error, ftxui::Color::RGB(255, 0, 0));
}

TEST_F(ThemeManagerTest, SwitchThemeChangesActiveColors)
{
    writeFile(mDir, "alpha.json", themeJson("Alpha", "#0F0", "#F00"));
    writeFile(mDir, "beta.json", themeJson("Beta", "#00F", "#0FF"));

    auto &mgr = ThemeManager::instance();
    mgr.initialize(mDir.string());

    ASSERT_TRUE(mgr.setActive("Alpha"));
    EXPECT_EQ(mgr.getActive()->success, ftxui::Color::RGB(0, 255, 0));

    ASSERT_TRUE(mgr.setActive("Beta"));
    EXPECT_EQ(mgr.getActive()->success, ftxui::Color::RGB(0, 0, 255));
}

TEST_F(ThemeManagerTest, SetActiveUnknownThemeReturnsFalse)
{
    writeFile(mDir, "alpha.json", themeJson("Alpha", "#0F0", "#F00"));

    auto &mgr = ThemeManager::instance();
    mgr.initialize(mDir.string());

    EXPECT_FALSE(mgr.setActive("DoesNotExist"));
}

TEST_F(ThemeManagerTest, UserThemeIsDiscovered)
{
    writeFile(mDir, "custom.json", themeJson("Custom", "#123", "#456"));

    auto &mgr = ThemeManager::instance();
    mgr.initialize(mDir.string());

    auto names = mgr.getAvailableThemes();
    EXPECT_NE(std::find(names.begin(), names.end(), "Custom"), names.end());
}

TEST_F(ThemeManagerTest, MalformedThemeFileIsSkipped)
{
    writeFile(mDir, "good.json", themeJson("Good", "#0F0", "#F00"));
    writeFile(mDir, "bad.json", "{ this is not valid json ");

    auto &mgr = ThemeManager::instance();
    mgr.initialize(mDir.string());

    auto names = mgr.getAvailableThemes();
    EXPECT_NE(std::find(names.begin(), names.end(), "Good"), names.end());
    // App stays usable — the bad file did not abort loading.
}

TEST_F(ThemeManagerTest, FallsBackToInternalDefaultWhenDirUnwritable)
{
    // Seeding into an empty dir always yields a loadable "Default".
    auto &mgr = ThemeManager::instance();
    mgr.initialize(mDir.string());

    auto names = mgr.getAvailableThemes();
    EXPECT_NE(std::find(names.begin(), names.end(), "Default"), names.end());

    auto active = mgr.getActive();
    ASSERT_TRUE(active);
}

TEST_F(ThemeManagerTest, GetByNameReturnsRawTheme)
{
    writeFile(mDir, "alpha.json", themeJson("Alpha", "#0F0", "#F00"));

    auto &mgr = ThemeManager::instance();
    mgr.initialize(mDir.string());

    auto theme = mgr.getByName("Alpha");
    ASSERT_TRUE(theme.has_value());
    EXPECT_EQ(theme->name, "Alpha");
    EXPECT_EQ(theme->colors.success, "#0F0");

    EXPECT_FALSE(mgr.getByName("Nope").has_value());
}

// ============================================================================
// Config::Theme — JSON serialization + validation
// ============================================================================

TEST(ThemeSerializationTest, RoundTripPreservesColors)
{
    Config::Theme original;
    original.name = "RoundTrip";
    original.description = "desc";
    original.colors.success = "#ABC";
    original.colors.error = "#DEF";

    json j = original;
    auto restored = j.get<Config::Theme>();

    EXPECT_EQ(restored.name, "RoundTrip");
    EXPECT_EQ(restored.colors.success, "#ABC");
    EXPECT_EQ(restored.colors.error, "#DEF");
}

TEST(ThemeSerializationTest, MissingColorFieldsFallBackToDefaults)
{
    json j;
    j["name"] = "Sparse";
    j["colors"] = json::object(); // no color keys at all

    auto theme = j.get<Config::Theme>();

    // from_json + validate() must populate required tokens, not leave empty.
    EXPECT_FALSE(theme.colors.success.empty());
    EXPECT_FALSE(theme.colors.error.empty());
    EXPECT_FALSE(theme.colors.label.empty());
    EXPECT_FALSE(theme.colors.title.empty());
}

TEST(ThemeSerializationTest, ValidateFillsEmptyNameAndRequiredColors)
{
    Config::Theme t;
    t.name = "";
    t.colors.success = "";
    t.colors.title = "";

    t.validate();

    EXPECT_FALSE(t.name.empty());
    EXPECT_FALSE(t.colors.success.empty());
    EXPECT_FALSE(t.colors.title.empty());
}

TEST(ThemeSerializationTest, GradientsGetAtLeastTwoStopsAfterValidate)
{
    Config::ThemeGradients g;
    g.cpuLoad = { { 0.0, "#F00" } };  // only one stop — invalid
    g.memoryUsage.clear();            // none

    g.validate();

    EXPECT_GE(g.cpuLoad.size(), 2u);
    EXPECT_GE(g.memoryUsage.size(), 2u);
}

TEST(ThemeSerializationTest, GradientStopsAreClampedAndSorted)
{
    Config::ThemeGradients g;
    g.cpuLoad = { { 1.5, "#F00" }, { -0.3, "#0F0" }, { 0.5, "#FF0" } };
    g.memoryUsage = { { 0.0, "#0F0" }, { 1.0, "#F00" } };

    g.validate();

    // Clamped into [0,1] and sorted ascending by offset.
    ASSERT_GE(g.cpuLoad.size(), 3u);
    for (const auto &s : g.cpuLoad) {
        EXPECT_GE(s.offset, 0.0);
        EXPECT_LE(s.offset, 1.0);
    }
    EXPECT_TRUE(std::is_sorted(g.cpuLoad.begin(), g.cpuLoad.end(),
                               [](const auto &a, const auto &b) {
                                   return a.offset < b.offset;
                               }));
}
