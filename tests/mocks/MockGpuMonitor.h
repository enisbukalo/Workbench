#pragma once

#include "IGpuMonitor.h"

#include <gmock/gmock.h>

class MockGpuMonitor : public IGpuMonitor
{
  public:
    MOCK_METHOD0(update, void());
    MOCK_CONST_METHOD0(getStats, std::vector<MemoryStats>());
    MOCK_CONST_METHOD0(getLoadStats, std::vector<ProcessorStats>());
};
