#pragma once

#include "ICpuMonitor.h"
#include "IGpuMonitor.h"
#include "IMemoryMonitor.h"
#include "IBatchStateTracker.h"
#include "IBatchStore.h"
#include "IModelInfoMonitor.h"
#include "IModelStateTracker.h"
#include "IModelsIni.h"
#include "ILlamaServerProcess.h"
#include "IConfigManager.h"

/**
 * @file appDependencies.h
 * @brief Aggregate struct holding references to all panel dependencies.
 *
 * This struct is constructed in app.cpp from singleton instances and passed
 * to panels that need them. It enables dependency injection without changing
 * singleton lifecycle logic elsewhere in the codebase.
 *
 * Usage:
 * @code
 * AppDependencies deps{
 *     ConfigManager::instance(),
 *     LlamaServerProcess::instance(),
 *     ModelInfoMonitor::instance(),
 *     ModelsIni::instance(),
 *     CpuMonitor::instance(),
 *     MemoryMonitor::instance(),
 *     GpuMonitor::instance()
 * };
 * SettingsPanel settingsPanel(deps);
 * @endcode
 */
struct AppDependencies
{
    IConfigManager &config;
    ILlamaServerProcess &server;
    IModelInfoMonitor &modelInfo;
    IModelStateTracker &tracker;
    IModelsIni &modelsIni;
    IBatchStore &batches;
    IBatchStateTracker &batchTracker;
    ICpuMonitor &cpu;
    IMemoryMonitor &mem;
    IGpuMonitor &gpu;
};
