#pragma once

#include "IBatchStore.h"

#include <gmock/gmock.h>

#include <optional>
#include <string>
#include <vector>

class MockBatchStore : public IBatchStore
{
  public:
	MOCK_CONST_METHOD0(getBatchNames, std::vector<std::string>());
	MOCK_CONST_METHOD1(getBatch, std::optional<Batch>(const std::string &name));
	MOCK_METHOD1(saveBatch, bool(const Batch &batch));
	MOCK_METHOD1(deleteBatch, bool(const std::string &name));
};
