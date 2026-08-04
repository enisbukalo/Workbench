#pragma once

#include "modelInfo.h"

/**
 * @file IVllmMonitor.h
 * @brief Thin interface capturing VllmMonitor methods used by panels.
 *
 * Panels depend on this interface rather than the singleton directly,
 * enabling unit testing with GMock. The real VllmMonitor implements
 * this interface directly (zero indirection overhead).
 */
class IVllmMonitor
{
  public:
	virtual ~IVllmMonitor() = default;

	/** @brief Returns the latest aggregate ModelInfo snapshot from the vLLM
	 *  server. */
	virtual ModelInfo getStats() const = 0;
};