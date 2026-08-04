/**
 * @file themeManager.cpp
 * @brief Implementation of ThemeManager singleton.
 *
 * Loads themes from disk (JSON), resolves hex color strings into ftxui::Color
 * values, builds LinearGradients for gauges, and subscribes to EventBus for
 * runtime theme switching via settings panel.
 */

#include "ThemeManager.h"

#include "configManager.h"
#include "eventBus.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>
#include <sstream>

namespace fs = std::filesystem;

using json = nlohmann::json;

// ============================================================================
// Singleton
// ============================================================================

ThemeManager &ThemeManager::instance()
{
	static ThemeManager mgr;
	return mgr;
}

// ============================================================================
// Initialization
// ============================================================================

void ThemeManager::initialize(const std::string &userPath)
{
	// Ensure the theme directory exists and always (re)write default.json from
	// the internal palette, so "Default" is guaranteed present and canonical.
	seedDefaultTheme(userPath);

	// Load themes under lock — protects mThemes / mAvailableNames mutation
	{
		std::scoped_lock lock(mMutex);

		mThemes.clear();
		mAvailableNames.clear();

		loadFromDirectory(userPath);

		if (mThemes.empty()) {
			// Disk seeding failed (e.g. unwritable dir) — keep app usable.
			spdlog::warn("No theme files loaded, using internal default");
			Config::Theme fallback = makeInternalDefault();
			fallback.name = "Default";
			mThemes["Default"] = std::move(fallback);
			mAvailableNames.push_back("Default");
		}

		// "Default" is always the leftmost pick; the rest stay alphabetical.
		auto it =
			std::find(mAvailableNames.begin(), mAvailableNames.end(), "Default");
		if (it != mAvailableNames.end()) {
			std::rotate(mAvailableNames.begin(), it, it + 1);
		}
	}

	// Activate current theme from config (or first available) — outside lock
	const auto cfg = ConfigManager::instance().getConfigSnapshot();
	std::string desired = cfg.ui.theme.empty() ? "default" : cfg.ui.theme;

	bool found = false;
	for (const auto &name : getAvailableThemes()) {
		if (name == desired) {
			found = setActive(name);
			break;
		}
	}

	if (!found) {
		// Case-insensitive fallback
		std::string lDesired = desired;
		std::transform(lDesired.begin(),
					   lDesired.end(),
					   lDesired.begin(),
					   ::tolower);

		for (const auto &name : getAvailableThemes()) {
			std::string lName = name;
			std::transform(lName.begin(), lName.end(), lName.begin(), ::tolower);
			if (lName == lDesired) {
				found = setActive(name);
				break;
			}
		}
	}

	auto names = getAvailableThemes();
	if (!found && !names.empty()) {
		spdlog::warn("Theme '{}' not found, falling back to '{}'",
					 desired,
					 names[0]);
		setActive(names[0]);
	}

	// Subscribe to theme change events from settings panel
	mEventSubId =
		EventBus::subscribe("config.ui.theme", ThemeManager::onThemeChanged);
}

void ThemeManager::loadFromDirectory(const std::string &path)
{
	if (!fs::exists(path) || !fs::is_directory(path)) {
		spdlog::debug("Theme directory '{}' not found, skipping", path);
		return;
	}

	std::vector<std::string> themeFiles;
	for (const auto &entry : fs::directory_iterator(path)) {
		if (entry.is_regular_file() && entry.path().extension() == ".json") {
			themeFiles.push_back(entry.path().string());
		}
	}

	std::ranges::sort(themeFiles);

	for (const auto &file : themeFiles) {
		try {
			std::ifstream ifs(file);
			if (!ifs.is_open())
				continue;

			json j = json::parse(ifs);
			Config::Theme theme = j.get<Config::Theme>();
			theme.validate();

			// Copy the name — it must outlive the std::move(theme) below.
			const std::string name = theme.name;
			auto it = mThemes.find(name);
			if (it != mThemes.end()) {
				spdlog::info("Theme '{}' overridden by '{}'", name, file);
			} else {
				mAvailableNames.push_back(name);
			}
			mThemes[name] = std::move(theme);

			spdlog::debug("Loaded theme '{}' from {}", name, file);
		} catch (const std::exception &e) {
			spdlog::warn("Failed to parse theme file '{}': {} — skipping",
						 file,
						 e.what());
		}
	}
}

void ThemeManager::seedDefaultTheme(const std::string &dir) const
{
	try {
		fs::create_directories(dir);

		Config::Theme def = makeInternalDefault();
		def.name = "Default";

		json j = def;
		fs::path out = fs::path(dir) / "default.json";

		std::ofstream ofs(out, std::ios::trunc);
		if (!ofs.is_open()) {
			spdlog::warn("Cannot write default theme to '{}'", out.string());
			return;
		}
		ofs << j.dump(2) << '\n';
		spdlog::debug("Seeded default theme at {}", out.string());
	} catch (const std::exception &e) {
		spdlog::warn("Failed to seed default theme in '{}': {}", dir, e.what());
	}
}

// ============================================================================
// Active theme access + switching
// ============================================================================

/**
 * @brief Get currently active resolved theme — lock-free snapshot.
 *
 * Atomically loads the active shared_ptr.  The caller holds a reference for as
 * long as it keeps the returned pointer, so a concurrent setActive() swap never
 * frees the theme out from under a render in progress.
 */
std::shared_ptr<const ThemeManager::ResolvedTheme>
ThemeManager::getActive() const noexcept
{
	auto snapshot = std::atomic_load(&mActive);
	if (!snapshot) {
		// Defensive: never hand back null before initialize() ran.
		static const auto kEmpty = std::make_shared<const ResolvedTheme>();
		return kEmpty;
	}
	return snapshot;
}

bool ThemeManager::setActive(const std::string &name)
{
	// Resolve under lock (reads mThemes), then publish via atomic swap.
	std::shared_ptr<const ResolvedTheme> resolved;
	{
		std::scoped_lock lock(mMutex);
		auto it = mThemes.find(name);
		if (it == mThemes.end()) {
			spdlog::warn("Cannot activate theme '{}': not found", name);
			return false;
		}
		resolved = std::make_shared<const ResolvedTheme>(resolve(it->second));
	}

	// Atomic pointer swap — readers see either the old or the new theme whole,
	// never a torn mix of fields.
	std::atomic_store(&mActive, resolved);
	spdlog::info("Activated theme '{}'", name);
	return true;
}

std::vector<std::string> ThemeManager::getAvailableThemes() const noexcept
{
	std::scoped_lock lock(mMutex);
	return mAvailableNames;
}

std::optional<Config::Theme>
ThemeManager::getByName(const std::string &name) const
{
	std::scoped_lock lock(mMutex);
	auto it = mThemes.find(name);
	if (it != mThemes.end())
		return it->second;
	return std::nullopt;
}

// ============================================================================
// Color resolution
// ============================================================================

ftxui::Color ThemeManager::parseHex(const std::string &hex) const noexcept
{
	// Handle empty string → terminal default
	if (hex.empty()) {
		return ftxui::Color::Default;
	}

	// Strip leading '#' if present
	std::string trimmed = hex;
	if (!trimmed.empty() && trimmed[0] == '#') {
		trimmed = trimmed.substr(1);
	}

	// Parse #RGB → expand to #RRGGBB
	if (trimmed.size() == 3) {
		std::string expanded;
		for (char c : trimmed)
			expanded += c, expanded += c;
		trimmed = expanded;
	}

	if (trimmed.size() != 6) {
		spdlog::warn("Invalid hex color '{}', falling back to Default", hex);
		return ftxui::Color::Default;
	}

	try {
		uint8_t r =
			static_cast<uint8_t>(std::stoi(trimmed.substr(0, 2), nullptr, 16));
		uint8_t g =
			static_cast<uint8_t>(std::stoi(trimmed.substr(2, 2), nullptr, 16));
		uint8_t b =
			static_cast<uint8_t>(std::stoi(trimmed.substr(4, 2), nullptr, 16));
		return ftxui::Color::RGB(r, g, b);
	} catch (...) {
		spdlog::warn("Failed to parse hex color '{}', falling back to Default",
					 hex);
		return ftxui::Color::Default;
	}
}

ThemeManager::ResolvedTheme
ThemeManager::resolve(const Config::Theme &theme) const
{
	auto p = [this](const std::string &hex) { return parseHex(hex); };

	ResolvedTheme r;
	r.success = p(theme.colors.success);
	r.warning = p(theme.colors.warning);
	r.error = p(theme.colors.error);
	r.info = p(theme.colors.info);
	r.label = p(theme.colors.label);
	r.title = p(theme.colors.title);
	r.text = p(theme.colors.text);
	r.mutedText = p(theme.colors.mutedText);
	r.separator = p(theme.colors.separator);
	r.toggleOn = p(theme.colors.toggleOn);
	r.toggleOff = p(theme.colors.toggleOff);
	r.focusAccent = p(theme.colors.focusAccent);
	r.gaugeBg = p(theme.colors.gaugeBg);
	r.tableHeaderBg = p(theme.colors.tableHeaderBg);
	r.tableHeaderText = p(theme.colors.tableHeaderText);
	r.tableAltEven = p(theme.colors.tableAltEven);
	r.tableAltOdd = p(theme.colors.tableAltOdd);
	r.capturingBorder = p(theme.colors.capturingBorder);
	r.activeTabBg = p(theme.colors.activeTabBg);
	r.selectionBg = p(theme.colors.selectionBg);
	r.subtleLabel = p(theme.colors.subtleLabel);
	r.deleteAction = p(theme.colors.deleteAction);

	// Build gradients: cpuLoad (90° vertical), memoryUsage (0° horizontal)
	r.cpuLoadGauge = buildGradient(theme.gradients.cpuLoad, 90);
	r.memoryUsageGauge = buildGradient(theme.gradients.memoryUsage, 0);
	return r;
}

ftxui::LinearGradient
ThemeManager::buildGradient(const std::vector<Config::GradientStop> &stops,
							int angle) const
{
	auto grad = ftxui::LinearGradient().Angle(static_cast<float>(angle));
	for (const auto &[offset, colorHex] : stops) {
		ftxui::Color c = parseHex(colorHex);
		float pos = static_cast<float>(std::clamp(offset, 0.0, 1.0));
		grad.Stop(c, pos);
	}
	return grad;
}

// ============================================================================
// Internal default fallback
// ============================================================================

Config::Theme ThemeManager::makeInternalDefault() const
{
	Config::Theme t;
	t.name = "Default";
	t.description = "Original Workbench color scheme (internal fallback)";

	// Colors — match current hardcoded FTXUI palette
	t.colors.success = "#008000";
	t.colors.warning = "#808000";
	t.colors.error = "#800000";
	t.colors.info = "#008080";
	t.colors.label = "#FF00FF";
	t.colors.title = "#808000";
	t.colors.text = "";
	t.colors.mutedText = "#808080";
	t.colors.separator = "#D78700";
	t.colors.toggleOn = "#00FF00";
	t.colors.toggleOff = "#008080";
	t.colors.focusAccent = "#FFFFFF";
	t.colors.gaugeBg = "#00005F";
	t.colors.tableHeaderBg = ""; // terminal default (transparent)
	t.colors.tableHeaderText = "#FFFFFF";
	t.colors.tableAltEven = "#008080";
	t.colors.tableAltOdd = "#FF00FF";
	t.colors.capturingBorder = "#87FF5F";
	t.colors.activeTabBg = "#FF00FF";
	t.colors.selectionBg = "#FFFF00";
	t.colors.subtleLabel = "#C0C0C0";
	t.colors.deleteAction = "#800000";

	// Gradients: cpuLoad (Red→Yellow→Green), memoryUsage (Green→Yellow→Red)
	t.gradients.cpuLoad = { { 0.0, "#FF0000" },
							{ 0.5, "#FFFF00" },
							{ 1.0, "#00FF00" } };
	t.gradients.memoryUsage = { { 0.0, "#00FF00" },
								{ 0.5, "#FFFF00" },
								{ 1.0, "#FF0000" } };

	return t;
}

// ============================================================================
// Event handler
// ============================================================================

void ThemeManager::onThemeChanged(const std::string &eventId, const void *data)
{
	(void)eventId; // "config.ui.theme"

	if (data) {
		const auto *newName = static_cast<const std::string *>(data);
		spdlog::info("Theme change event received: '{}'", newName->c_str());
		auto &mgr = ThemeManager::instance();
		if (!mgr.setActive(*newName)) {
			spdlog::warn("Failed to switch to theme '{}'", newName->c_str());
		}
	}
}
