#pragma once

#include "IModelStateTracker.h"

#include <gmock/gmock.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

class MockModelStateTracker : public IModelStateTracker
{
  public:
    MOCK_METHOD1(requestLoad, void(const std::string &));
    MOCK_METHOD1(requestUnload, void(const std::string &));
    MOCK_METHOD0(requestUnloadAll, void());
    MOCK_METHOD3(ingestPoll,
                 void(const std::vector<std::string> &,
                      const std::function<WorkerLiveness(const std::string &)> &,
                      const std::function<ModelInfo(const std::string &)> &));
    MOCK_METHOD0(onServerOffline, void());
    MOCK_CONST_METHOD0(snapshot, std::map<std::string, ModelState>());
    MOCK_CONST_METHOD0(shouldSkipModelQueries, bool());
    MOCK_METHOD0(takeCrashed, std::vector<std::string>());
};
