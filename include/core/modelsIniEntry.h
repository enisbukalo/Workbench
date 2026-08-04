#pragma once

#include <string>

/**
 * @struct ModelsIniEntry
 * @brief A unique model entry from models.ini: one record per distinct GGUF path.
 *
 * Defined here so both IModelsIni and ModelsIni can reference it without
 * circular includes. Sections that share the same "model" path (i.e. presets)
 * are deduplicated; only the first section encountered for each path is returned.
 */
struct ModelsIniEntry
{
	std::string displayName; // section name (first section found for this path)
	std::string modelPath;   // the GGUF file path
};
