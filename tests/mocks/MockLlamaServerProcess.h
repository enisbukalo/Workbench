#pragma once

#include "ILlamaServerProcess.h"

#include <gmock/gmock.h>

#include <string>
#include <vector>

class MockLlamaServerProcess : public ILlamaServerProcess
{
  public:
    MOCK_CONST_METHOD0(isRunning, bool());
    MOCK_METHOD2(launch, bool(const std::string &modelPath, const Config::ServerSettings &server));
    MOCK_METHOD0(terminate, bool());
    MOCK_METHOD0(isModelLoaded, bool());
    MOCK_METHOD0(getLoadedModelPath, std::string());
    MOCK_METHOD0(getLoadedModelIds, std::vector<std::string>());
    MOCK_METHOD0(unloadModel, bool());
    MOCK_METHOD1(unloadModel, bool(const std::string &modelName));
    MOCK_METHOD0(unloadAllModels, bool());
    MOCK_METHOD1(loadModel, bool(const std::string &modelPath));
    MOCK_METHOD0(isServerHealthy, bool());
};
