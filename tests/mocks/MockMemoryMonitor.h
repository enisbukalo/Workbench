#pragma once

#include "IMemoryMonitor.h"

#include <gmock/gmock.h>

class MockMemoryMonitor : public IMemoryMonitor
{
  public:
    MOCK_METHOD0(update, void());
    MOCK_CONST_METHOD0(getStats, MemoryStats());
};
