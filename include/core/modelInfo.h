#pragma once

#include <cstdint>
#include <string>

/**
 * @struct ModelInfo
 * @brief Data structure holding the current model metrics.
 *
 * Defined here so both IModelInfoMonitor and ModelInfoMonitor can reference it
 * without circular includes.
 */
/**
 * @enum ActivityState
 * @brief What the loaded model is doing right now.
 *
 * IDLE       — no slot is processing.
 * PROMPT     — a slot is processing but no tokens decoded yet (prefill).
 * GENERATING — a slot has decoded at least one token (streaming output).
 */
enum class ActivityState
{
	IDLE,
	PROMPT,
	GENERATING
};

struct ModelInfo
{
	std::string loadedModel;		// e.g., "nvidia_Orchestrator-8B-Q6_K_L"
	double generationTokensPerSec;	// tokens predicted per second
	double processingTokensPerSec;	// prompt tokens per second
	uint64_t totalPromptTokens;		// total prompt tokens processed
	uint64_t totalGenerationTokens; // total generation tokens processed
	int activeRequestCount;			// number of active requests
	bool isIdle;					// true if all slots idle
	bool isServerRunning;			// server is healthy
	bool isModelLoaded;				// a model is loaded
	// Finer-grained activity than isIdle: distinguishes prompt prefill from
	// token generation for the Model Info panel. Kept in sync with isIdle.
	ActivityState activityState = ActivityState::IDLE;
};
