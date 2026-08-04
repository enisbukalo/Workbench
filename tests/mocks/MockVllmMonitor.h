#pragma once

#include "IVllmMonitor.h"
#include <gmock/gmock.h>

/**
 * @file MockVllmMonitor.h
 * @brief GMock implementation of IVllmMonitor for unit testing.
 *
 * Allows panels to be tested with controlled vLLM stats without a real server.
 */
class MockVllmMonitor : public IVllmMonitor
{
  public:
	MOCK_CONST_METHOD0(getStats, ModelInfo());
};