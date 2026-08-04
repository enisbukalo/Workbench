#include "modelPathValidation.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

namespace pathvalid {

namespace {

/** Lowercase a copy of @p s using the classic C locale (ASCII only). */
std::string toLowerAscii(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return s;
}

/** True if @p ext (e.g. ".GGUF") equals ".gguf" case-insensitively. */
bool isGgufExtension(const std::string &ext)
{
	return toLowerAscii(ext) == ".gguf";
}

} // namespace

std::string cleanPath(const std::string &rawPath)
{
	std::string s = rawPath;

	// Trim leading/trailing whitespace.
	const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
	s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());

	// Strip a single matched pair of surrounding quotes (" or ').
	if (s.size() >= 2) {
		const char front = s.front();
		const char back = s.back();
		if ((front == '"' && back == '"') || (front == '\'' && back == '\'')) {
			s = s.substr(1, s.size() - 2);
		}
	}

	return s;
}

std::string validateGgufPath(const std::string &rawPath)
{
	const std::string path = cleanPath(rawPath);
	if (path.empty()) {
		return "Path is empty";
	}

	std::error_code ec;
	const fs::path fsPath(path);

	if (!fs::exists(fsPath, ec) || ec) {
		return "Path does not exist";
	}
	if (!fs::is_regular_file(fsPath, ec) || ec) {
		return "Path is not a file";
	}
	if (!isGgufExtension(fsPath.extension().string())) {
		return "File is not a .gguf";
	}

	return "";
}

std::string deriveModelName(const std::string &rawPath)
{
	const std::string path = cleanPath(rawPath);
	if (path.empty()) {
		return "";
	}
	return fs::path(path).stem().string();
}

} // namespace pathvalid
