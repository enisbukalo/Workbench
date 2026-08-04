#pragma once

#include <string>

/**
 * @file modelPathValidation.h
 * @brief Cross-platform validation and naming for user-supplied GGUF paths.
 *
 * Used by the "Add New Model" popup in ModelsPanel to validate a free-text
 * path before writing a models.ini entry. Built entirely on std::filesystem,
 * so path handling is platform-agnostic (Linux + Windows) with no custom
 * separator logic. Free functions (no UI, no global state) so the logic is
 * unit-testable in isolation.
 *
 * @note Shell '~' expansion is intentionally not supported: it is a shell
 *       convention, not a filesystem one, and std::filesystem does not resolve
 *       it. Users supply an absolute or relative path.
 */
namespace pathvalid
{

/**
 * @brief Trim surrounding whitespace and a single pair of surrounding quotes.
 *
 * Handles paths pasted with wrapping double or single quotes (common when
 * copying from a file manager or shell). Does not touch interior characters.
 *
 * @param rawPath Path as typed/pasted by the user.
 * @return Cleaned path. Never throws.
 */
[[nodiscard]] std::string cleanPath(const std::string &rawPath);

/**
 * @brief Validate that @p rawPath points to an existing regular .gguf file.
 *
 * Cleans @p rawPath via cleanPath(), then uses std::filesystem to check that
 * the target exists, is a regular file, and has a ".gguf" extension
 * (case-insensitive). All filesystem errors are reported via the return
 * value rather than thrown.
 *
 * @param rawPath Path as typed/pasted by the user.
 * @return Empty string when valid; otherwise a human-readable error message.
 */
[[nodiscard]] std::string validateGgufPath(const std::string &rawPath);

/**
 * @brief Derive a display/model name from a GGUF path.
 *
 * Returns the filename stem (std::filesystem::path::stem), i.e. the filename
 * with its final extension removed. Example:
 * "/models/Mistral-7B.gguf" -> "Mistral-7B". The input is cleaned first so
 * quoted paths work too.
 *
 * @param rawPath Path to a .gguf file.
 * @return The bare model name, or an empty string if none can be derived.
 */
[[nodiscard]] std::string deriveModelName(const std::string &rawPath);

} // namespace pathvalid
