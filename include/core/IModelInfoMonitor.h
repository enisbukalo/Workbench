#pragma once

#include "modelInfo.h"

#include <map>
#include <string>

/**
 * @file IModelInfoMonitor.h
 * @brief Thin interface capturing ModelInfoMonitor methods used by panels.
 *
 * Panels depend on this interface rather than the singleton directly,
 * enabling unit testing with GMock. The real ModelInfoMonitor implements
 * this interface directly (zero indirection overhead).
 */
class IModelInfoMonitor
{
  public:
    virtual ~IModelInfoMonitor() = default;

    /** @brief Returns the latest aggregate ModelInfo snapshot. */
    virtual ModelInfo getStats() const = 0;

    /** @brief Stats for one loaded model by id (models.ini section name);
     *  default-constructed ModelInfo when unknown. */
    virtual ModelInfo getStatsFor(const std::string &name) const = 0;

    /** @brief Snapshot of every loaded model's stats, keyed by model id. */
    virtual std::map<std::string, ModelInfo> getAllStats() const = 0;
};
