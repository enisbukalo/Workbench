#pragma once

#include "IModelsIni.h"

#include <gmock/gmock.h>

#include <optional>
#include <string>
#include <vector>

class MockModelsIni : public IModelsIni
{
  public:
    MOCK_CONST_METHOD0(getUniqueModelEntries, std::vector<ModelsIniEntry>());
    MOCK_CONST_METHOD1(getModelPath, std::string(const std::string &sectionName));
    MOCK_CONST_METHOD1(getPresetsForModel, std::vector<Config::ModelPreset>(const std::string &modelPath));
    MOCK_METHOD1(savePreset, bool(const Config::ModelPreset &preset));
    MOCK_METHOD2(renamePreset, bool(const std::string &oldName, const std::string &newName));
    MOCK_METHOD1(deletePreset, bool(const std::string &sectionName));
    MOCK_CONST_METHOD0(getModelNames, std::vector<std::string>());
    MOCK_CONST_METHOD1(getPreset, std::optional<Config::ModelPreset>(const std::string &sectionName));
};
