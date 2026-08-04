#pragma once

#include "ICpuMonitor.h"

#include <gmock/gmock.h>

class MockCpuMonitor : public ICpuMonitor
{
  public:
    MOCK_METHOD0(update, void());
    MOCK_CONST_METHOD0(getStats, ProcessorStats());
};
