#pragma once

#include "IModelInfoMonitor.h"

#include <gmock/gmock.h>

class MockModelInfoMonitor : public IModelInfoMonitor
{
  public:
    MOCK_CONST_METHOD0(getStats, ModelInfo());
    MOCK_CONST_METHOD1(getStatsFor, ModelInfo(const std::string &));
    MOCK_CONST_METHOD0(getAllStats, std::map<std::string, ModelInfo>());
};
