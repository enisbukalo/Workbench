#pragma once

#include "modelSettings.h"
#include "modelsIniEntry.h"

#include <optional>
#include <string>
#include <vector>

/**
 * @file IModelsIni.h
 * @brief Thin interface capturing ModelsIni methods used by panels.
 *
 * Panels depend on this interface rather than the singleton directly,
 * enabling unit testing with GMock. The real ModelsIni implements
 * this interface directly (zero indirection overhead).
 */
class IModelsIni
{
  public:
    virtual ~IModelsIni() = default;

    /** @brief Return one entry per unique GGUF path in models.ini. */
    virtual std::vector<ModelsIniEntry> getUniqueModelEntries() const = 0;

    /** @brief Get the model path for a given section name. */
    virtual std::string getModelPath(const std::string &sectionName) const = 0;

    /** @brief Return all presets whose "model" key matches the given path. */
    virtual std::vector<Config::ModelPreset> getPresetsForModel(const std::string &modelPath) const = 0;

    /** @brief Persist a preset. @return true on success. */
    virtual bool savePreset(const Config::ModelPreset &preset) = 0;

    /** @brief Rename a section in-place. @return true on success. */
    virtual bool renamePreset(const std::string &oldName, const std::string &newName) = 0;

    /** @brief Delete a section from the INI file. @return true on success. */
    virtual bool deletePreset(const std::string &sectionName) = 0;

    /** @brief Get all model section names (excluding global [*]). */
    virtual std::vector<std::string> getModelNames() const = 0;

    /** @brief Parse a section into a ModelPreset. @return nullopt if section unknown. */
    virtual std::optional<Config::ModelPreset> getPreset(const std::string &sectionName) const = 0;
};
