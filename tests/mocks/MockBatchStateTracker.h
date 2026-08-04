#pragma once

#include "IBatchStateTracker.h"

#include <gmock/gmock.h>

#include <map>
#include <string>
#include <vector>

class MockBatchStateTracker : public IBatchStateTracker
{
  public:
	MOCK_CONST_METHOD2(statusOf,
					   BatchLifecycle(const std::vector<std::string> &presets,
									  const std::map<std::string, ModelState>
										  &models));
};
