#pragma once

#include "IConfigManager.h"

#include <gmock/gmock.h>

class MockConfigManager : public IConfigManager
{
  public:
    MOCK_METHOD(Config::UserConfig, getConfigSnapshot, (), (const, override));
    MOCK_METHOD(Config::ServerSettings, getServerSettings, (), (const, override));
    MOCK_METHOD(void, setConfig, (const Config::UserConfig &), (override));
    MOCK_METHOD(std::vector<Config::TerminalPreset>, getTerminalPresets, (), (const, override));
    MOCK_CONST_METHOD1(findTerminalPreset, std::optional<Config::TerminalPreset>(const std::string &name));
    MOCK_METHOD1(addTerminalPreset, bool(Config::TerminalPreset preset));
    MOCK_METHOD1(removeTerminalPreset, bool(const std::string &name));
    MOCK_METHOD2(updateTerminalPreset, bool(const std::string &oldName, Config::TerminalPreset preset));
    MOCK_METHOD0(save, bool());
};
