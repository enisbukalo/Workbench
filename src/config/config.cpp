/**
 * @file config.cpp
 * @brief JSON serialization functions for configuration structures.
 *
 * Implements to_json/from_json functions for all Config structures
 * using nlohmann::json. These functions enable automatic serialization
 * and deserialization of the UserConfig structure.
 *
 * The serialization format is:
 * @code
 * {
 *   "server": { ... },
 *   "ui": { ... },
 *   "terminal": { ... },
 *   "presets": [ ... ]   // each preset carries its own load/inference
 * }
 * @endcode
 */

#include "config.h"

#include <array>
#include <cmath>
#include <iomanip>
#include <locale>
#include <spdlog/spdlog.h>
#include <sstream>

using json = nlohmann::json;

namespace Config {

/**
 * @brief Round a double value to exactly 2 decimal places.
 *
 * This ensures clean JSON output (e.g., 0.00 instead of 0.00000 etc..)
 * and matches the precision that consuming code will use.
 *
 * @param value The double value to round
 * @return The value rounded to 2 decimal places
 */
static double roundToTwoDecimals(double value)
{
	return std::round(value * 100.0) / 100.0;
}

// ============================================================================
// ApiSettings serialization
// ============================================================================

void to_json(json &j, const ApiSettings &v)
{
	j = json{ { "apiEnabled", v.apiEnabled },
			  { "apiHost", v.apiHost },
			  { "apiPort", v.apiPort },
			  { "apiRequireKey", v.apiRequireKey },
			  { "apiKey", v.apiKey } };
}

void from_json(const json &j, ApiSettings &v)
{
	v.apiEnabled = j.value("apiEnabled", v.apiEnabled);
	v.apiHost = j.value("apiHost", v.apiHost);
	v.apiPort = j.value("apiPort", v.apiPort);
	v.apiRequireKey = j.value("apiRequireKey", v.apiRequireKey);
	v.apiKey = j.value("apiKey", v.apiKey);
	v.validate();
}

// ============================================================================
// ServerSettings serialization
// ============================================================================

void to_json(json &j, const ServerSettings &v)
{
	j["executablePath"] = v.executablePath;
	j["host"] = v.host;
	j["port"] = v.port;
	j["apiKey"] = v.apiKey;
	j["apiKeyFile"] = v.apiKeyFile;
	j["timeout"] = v.timeout;
	j["threadsHttp"] = v.threadsHttp;
	j["reusePort"] = v.reusePort;

	j["sslKeyFile"] = v.sslKeyFile;
	j["sslCertFile"] = v.sslCertFile;

	j["path"] = v.path;
	j["apiPrefix"] = v.apiPrefix;
	j["mediaPath"] = v.mediaPath;

	j["alias"] = v.alias;
	j["ui"] = v.ui;
	j["uiConfig"] = v.uiConfig;
	j["uiConfigFile"] = v.uiConfigFile;
	j["uiMcpProxy"] = v.uiMcpProxy;
	j["tools"] = v.tools;
	j["embedding"] = v.embedding;
	j["reranking"] = v.reranking;
	j["contBatching"] = v.contBatching;
	j["cachePrompt"] = v.cachePrompt;
	j["cacheReuse"] = v.cacheReuse;
	j["cacheRam"] = v.cacheRam;
	j["ctxCheckpoints"] = v.ctxCheckpoints;
	j["contextShift"] = v.contextShift;
	j["warmup"] = v.warmup;
	j["jinja"] = v.jinja;
	j["prefillAssistant"] = v.prefillAssistant;
	j["slotPromptSimilarity"] = roundToTwoDecimals(v.slotPromptSimilarity);
	j["sleepIdleSeconds"] = v.sleepIdleSeconds;

	j["props"] = v.props;
	j["slots"] = v.slots;
	j["slotSavePath"] = v.slotSavePath;
	j["modelsMax"] = v.modelsMax;
	j["modelsAutoload"] = v.modelsAutoload;
	j["checkpointMinStep"] = v.checkpointMinStep;
	j["cacheIdleSlots"] = v.cacheIdleSlots;
	j["pooling"] = v.pooling;
	j["embdNormalize"] = v.embdNormalize;
	j["reasoning"] = v.reasoning;
	j["reasoningBudget"] = v.reasoningBudget;
}

void from_json(const json &j, ServerSettings &v)
{
	v.executablePath = j.value("executablePath", v.executablePath);
	v.host = j.value("host", v.host);
	v.port = j.value("port", v.port);
	v.apiKey = j.value("apiKey", v.apiKey);
	v.apiKeyFile = j.value("apiKeyFile", v.apiKeyFile);
	v.timeout = j.value("timeout", v.timeout);
	v.threadsHttp = j.value("threadsHttp", v.threadsHttp);
	v.reusePort = j.value("reusePort", v.reusePort);

	v.sslKeyFile = j.value("sslKeyFile", v.sslKeyFile);
	v.sslCertFile = j.value("sslCertFile", v.sslCertFile);

	v.path = j.value("path", v.path);
	v.apiPrefix = j.value("apiPrefix", v.apiPrefix);
	v.mediaPath = j.value("mediaPath", v.mediaPath);

	v.alias = j.value("alias", v.alias);
	v.ui = j.value("ui", v.ui);
	v.uiConfig = j.value("uiConfig", v.uiConfig);
	v.uiConfigFile = j.value("uiConfigFile", v.uiConfigFile);
	v.uiMcpProxy = j.value("uiMcpProxy", v.uiMcpProxy);
	v.tools = j.value("tools", v.tools);
	v.embedding = j.value("embedding", v.embedding);
	v.reranking = j.value("reranking", v.reranking);
	v.contBatching = j.value("contBatching", v.contBatching);
	v.cachePrompt = j.value("cachePrompt", v.cachePrompt);
	v.cacheReuse = j.value("cacheReuse", v.cacheReuse);
	v.cacheRam = j.value("cacheRam", v.cacheRam);
	v.ctxCheckpoints = j.value("ctxCheckpoints", v.ctxCheckpoints);
	v.contextShift = j.value("contextShift", v.contextShift);
	v.warmup = j.value("warmup", v.warmup);
	v.jinja = j.value("jinja", v.jinja);
	v.prefillAssistant = j.value("prefillAssistant", v.prefillAssistant);
	v.slotPromptSimilarity =
		j.value("slotPromptSimilarity", v.slotPromptSimilarity);
	v.sleepIdleSeconds = j.value("sleepIdleSeconds", v.sleepIdleSeconds);

	// metrics is always enabled - do not read from config
	v.props = j.value("props", v.props);
	v.slots = j.value("slots", v.slots);
	v.slotSavePath = j.value("slotSavePath", v.slotSavePath);
	v.modelsMax = j.value("modelsMax", v.modelsMax);
	v.modelsAutoload = j.value("modelsAutoload", v.modelsAutoload);
	v.checkpointMinStep = j.value("checkpointMinStep", v.checkpointMinStep);
	v.cacheIdleSlots = j.value("cacheIdleSlots", v.cacheIdleSlots);
	v.pooling = j.value("pooling", v.pooling);
	v.embdNormalize = j.value("embdNormalize", v.embdNormalize);
	v.reasoning = j.value("reasoning", v.reasoning);
	v.reasoningBudget = j.value("reasoningBudget", v.reasoningBudget);
	v.validate();
}

// ============================================================================
// LoadSettings serialization
// ============================================================================

void to_json(json &j, const LoadSettings &v)
{
	j["modelPath"] = v.modelPath;
	j["modelUrl"] = v.modelUrl;
	j["hfRepo"] = v.hfRepo;
	j["hfFile"] = v.hfFile;
	j["hfToken"] = v.hfToken;

	j["ngpuLayers"] = v.ngpuLayers;
	j["splitMode"] = v.splitMode;
	j["tensorSplit"] = v.tensorSplit;
	j["devicePriority"] = v.devicePriority;

	j["ctxSize"] = v.ctxSize;
	j["batchSize"] = v.batchSize;
	j["ubatchSize"] = v.ubatchSize;
	j["parallel"] = v.parallel;

	j["cacheTypeK"] = v.cacheTypeK;
	j["cacheTypeV"] = v.cacheTypeV;
	j["kvOffload"] = v.kvOffload;
	j["kvUnified"] = v.kvUnified;

	j["flashAttn"] = v.flashAttn;
	j["mlock"] = v.mlock;
	j["mmap"] = v.mmap;

	j["threads"] = v.threads;
	j["threadsBatch"] = v.threadsBatch;

	j["lora"] = v.lora;
	j["mmproj"] = v.mmproj;

	j["modelDraft"] = v.modelDraft;
	j["draftMax"] = v.draftMax;
	j["specType"] = v.specType;
	j["cacheTypeKDraft"] = v.cacheTypeKDraft;
	j["cacheTypeVDraft"] = v.cacheTypeVDraft;
	j["deviceDraft"] = v.deviceDraft;

	j["chatTemplate"] = v.chatTemplate;
	j["reasoningFormat"] = v.reasoningFormat;
	j["preserveThinking"] = v.preserveThinking;

	j["fit"] = v.fit;

	// Issue #103 section C
	j["nCpuMoe"] = v.nCpuMoe;
	j["cpuMoe"] = v.cpuMoe;
	j["overrideTensor"] = v.overrideTensor;
	j["ropeScaling"] = v.ropeScaling;
	j["ropeScale"] = roundToTwoDecimals(v.ropeScale);
	j["ropeFreqBase"] = roundToTwoDecimals(v.ropeFreqBase);
	j["ropeFreqScale"] = roundToTwoDecimals(v.ropeFreqScale);
	j["yarnOrigCtx"] = v.yarnOrigCtx;
	j["yarnExtFactor"] = roundToTwoDecimals(v.yarnExtFactor);
	j["yarnAttnFactor"] = roundToTwoDecimals(v.yarnAttnFactor);
	j["yarnBetaSlow"] = roundToTwoDecimals(v.yarnBetaSlow);
	j["yarnBetaFast"] = roundToTwoDecimals(v.yarnBetaFast);
	j["swaFull"] = v.swaFull;
	j["keep"] = v.keep;
	j["numa"] = v.numa;
	j["fitTarget"] = v.fitTarget;
	j["fitCtx"] = v.fitCtx;
	j["checkTensors"] = v.checkTensors;
	j["overrideKv"] = v.overrideKv;
	j["loraScaled"] = v.loraScaled;
	j["controlVector"] = v.controlVector;
	j["controlVectorScaled"] = v.controlVectorScaled;
	j["specDraftNMin"] = v.specDraftNMin;
	j["specDraftPMin"] = roundToTwoDecimals(v.specDraftPMin);
	j["specDraftPSplit"] = roundToTwoDecimals(v.specDraftPSplit);
	j["cpuMoeDraft"] = v.cpuMoeDraft;
}

void from_json(const json &j, LoadSettings &v)
{
	v.modelPath = j.value("modelPath", v.modelPath);
	v.modelUrl = j.value("modelUrl", v.modelUrl);
	v.hfRepo = j.value("hfRepo", v.hfRepo);
	v.hfFile = j.value("hfFile", v.hfFile);
	v.hfToken = j.value("hfToken", v.hfToken);

	v.ngpuLayers = j.value("ngpuLayers", v.ngpuLayers);
	v.splitMode = j.value("splitMode", v.splitMode);
	v.tensorSplit = j.value("tensorSplit", v.tensorSplit);
	v.devicePriority = j.value("devicePriority", v.devicePriority);

	v.ctxSize = j.value("ctxSize", v.ctxSize);
	v.batchSize = j.value("batchSize", v.batchSize);
	v.ubatchSize = j.value("ubatchSize", v.ubatchSize);
	v.parallel = j.value("parallel", v.parallel);

	v.cacheTypeK = j.value("cacheTypeK", v.cacheTypeK);
	v.cacheTypeV = j.value("cacheTypeV", v.cacheTypeV);
	v.kvOffload = j.value("kvOffload", v.kvOffload);
	v.kvUnified = j.value("kvUnified", v.kvUnified);

	v.flashAttn = j.value("flashAttn", v.flashAttn);
	v.mlock = j.value("mlock", v.mlock);
	v.mmap = j.value("mmap", v.mmap);

	v.threads = j.value("threads", v.threads);
	v.threadsBatch = j.value("threadsBatch", v.threadsBatch);

	v.lora = j.value("lora", v.lora);
	v.mmproj = j.value("mmproj", v.mmproj);

	v.modelDraft = j.value("modelDraft", v.modelDraft);
	v.draftMax = j.value("draftMax", v.draftMax);
	v.specType = j.value("specType", v.specType);
	v.cacheTypeKDraft = j.value("cacheTypeKDraft", v.cacheTypeKDraft);
	v.cacheTypeVDraft = j.value("cacheTypeVDraft", v.cacheTypeVDraft);
	v.deviceDraft = j.value("deviceDraft", v.deviceDraft);

	v.chatTemplate = j.value("chatTemplate", v.chatTemplate);
	v.reasoningFormat = j.value("reasoningFormat", v.reasoningFormat);
	v.preserveThinking = j.value("preserveThinking", v.preserveThinking);

	v.fit = j.value("fit", v.fit);

	// Issue #103 section C
	v.nCpuMoe = j.value("nCpuMoe", v.nCpuMoe);
	v.cpuMoe = j.value("cpuMoe", v.cpuMoe);
	v.overrideTensor = j.value("overrideTensor", v.overrideTensor);
	v.ropeScaling = j.value("ropeScaling", v.ropeScaling);
	v.ropeScale = j.value("ropeScale", v.ropeScale);
	v.ropeFreqBase = j.value("ropeFreqBase", v.ropeFreqBase);
	v.ropeFreqScale = j.value("ropeFreqScale", v.ropeFreqScale);
	v.yarnOrigCtx = j.value("yarnOrigCtx", v.yarnOrigCtx);
	v.yarnExtFactor = j.value("yarnExtFactor", v.yarnExtFactor);
	v.yarnAttnFactor = j.value("yarnAttnFactor", v.yarnAttnFactor);
	v.yarnBetaSlow = j.value("yarnBetaSlow", v.yarnBetaSlow);
	v.yarnBetaFast = j.value("yarnBetaFast", v.yarnBetaFast);
	v.swaFull = j.value("swaFull", v.swaFull);
	v.keep = j.value("keep", v.keep);
	v.numa = j.value("numa", v.numa);
	v.fitTarget = j.value("fitTarget", v.fitTarget);
	v.fitCtx = j.value("fitCtx", v.fitCtx);
	v.checkTensors = j.value("checkTensors", v.checkTensors);
	v.overrideKv = j.value("overrideKv", v.overrideKv);
	v.loraScaled = j.value("loraScaled", v.loraScaled);
	v.controlVector = j.value("controlVector", v.controlVector);
	v.controlVectorScaled =
		j.value("controlVectorScaled", v.controlVectorScaled);
	v.specDraftNMin = j.value("specDraftNMin", v.specDraftNMin);
	v.specDraftPMin = j.value("specDraftPMin", v.specDraftPMin);
	v.specDraftPSplit = j.value("specDraftPSplit", v.specDraftPSplit);
	v.cpuMoeDraft = j.value("cpuMoeDraft", v.cpuMoeDraft);
	v.validate();
}

// ============================================================================
// InferenceSettings serialization
// ============================================================================

void to_json(json &j, const InferenceSettings &v)
{
	j["nPredict"] = v.nPredict;
	j["samplers"] = v.samplers;

	j["seed"] = v.seed;
	j["temperature"] = roundToTwoDecimals(v.temperature);
	j["topK"] = v.topK;
	j["topP"] = roundToTwoDecimals(v.topP);
	j["minP"] = roundToTwoDecimals(v.minP);
	j["topNsigma"] = roundToTwoDecimals(v.topNsigma);
	j["typicalP"] = roundToTwoDecimals(v.typicalP);

	j["xtcProbability"] = roundToTwoDecimals(v.xtcProbability);
	j["xtcThreshold"] = roundToTwoDecimals(v.xtcThreshold);

	j["repeatLastN"] = v.repeatLastN;
	j["repeatPenalty"] = roundToTwoDecimals(v.repeatPenalty);
	j["presencePenalty"] = roundToTwoDecimals(v.presencePenalty);
	j["frequencyPenalty"] = roundToTwoDecimals(v.frequencyPenalty);

	j["dryMultiplier"] = roundToTwoDecimals(v.dryMultiplier);
	j["dryBase"] = roundToTwoDecimals(v.dryBase);
	j["dryAllowedLength"] = v.dryAllowedLength;
	j["dryPenaltyLastN"] = v.dryPenaltyLastN;

	j["dynatempRange"] = roundToTwoDecimals(v.dynatempRange);
	j["dynatempExp"] = roundToTwoDecimals(v.dynatempExp);

	j["mirostat"] = v.mirostat;
	j["mirostatLr"] = roundToTwoDecimals(v.mirostatLr);
	j["mirostatEnt"] = roundToTwoDecimals(v.mirostatEnt);

	j["grammar"] = v.grammar;
	j["jsonSchema"] = v.jsonSchema;
	j["jsonSchemaFile"] = v.jsonSchemaFile;
	j["grammarFile"] = v.grammarFile;
	j["samplerSeq"] = v.samplerSeq;
	j["drySequenceBreaker"] = v.drySequenceBreaker;
	j["adaptiveTarget"] = roundToTwoDecimals(v.adaptiveTarget);
	j["adaptiveDecay"] = roundToTwoDecimals(v.adaptiveDecay);
	j["ignoreEos"] = v.ignoreEos;
	j["logitBias"] = v.logitBias;
	j["backendSampling"] = v.backendSampling;
}

void from_json(const json &j, InferenceSettings &v)
{
	v.nPredict = j.value("nPredict", v.nPredict);
	v.samplers = j.value("samplers", v.samplers);

	v.seed = j.value("seed", v.seed);
	v.temperature = j.value("temperature", v.temperature);
	v.topK = j.value("topK", v.topK);
	v.topP = j.value("topP", v.topP);
	v.minP = j.value("minP", v.minP);
	v.topNsigma = j.value("topNsigma", v.topNsigma);
	v.typicalP = j.value("typicalP", v.typicalP);

	v.xtcProbability = j.value("xtcProbability", v.xtcProbability);
	v.xtcThreshold = j.value("xtcThreshold", v.xtcThreshold);

	v.repeatLastN = j.value("repeatLastN", v.repeatLastN);
	v.repeatPenalty = j.value("repeatPenalty", v.repeatPenalty);
	v.presencePenalty = j.value("presencePenalty", v.presencePenalty);
	v.frequencyPenalty = j.value("frequencyPenalty", v.frequencyPenalty);

	v.dryMultiplier = j.value("dryMultiplier", v.dryMultiplier);
	v.dryBase = j.value("dryBase", v.dryBase);
	v.dryAllowedLength = j.value("dryAllowedLength", v.dryAllowedLength);
	v.dryPenaltyLastN = j.value("dryPenaltyLastN", v.dryPenaltyLastN);

	v.dynatempRange = j.value("dynatempRange", v.dynatempRange);
	v.dynatempExp = j.value("dynatempExp", v.dynatempExp);

	v.mirostat = j.value("mirostat", v.mirostat);
	v.mirostatLr = j.value("mirostatLr", v.mirostatLr);
	v.mirostatEnt = j.value("mirostatEnt", v.mirostatEnt);

	v.grammar = j.value("grammar", v.grammar);
	v.jsonSchema = j.value("jsonSchema", v.jsonSchema);
	v.jsonSchemaFile = j.value("jsonSchemaFile", v.jsonSchemaFile);
	v.grammarFile = j.value("grammarFile", v.grammarFile);
	v.samplerSeq = j.value("samplerSeq", v.samplerSeq);
	v.drySequenceBreaker = j.value("drySequenceBreaker", v.drySequenceBreaker);
	v.adaptiveTarget = j.value("adaptiveTarget", v.adaptiveTarget);
	v.adaptiveDecay = j.value("adaptiveDecay", v.adaptiveDecay);
	v.ignoreEos = j.value("ignoreEos", v.ignoreEos);
	v.logitBias = j.value("logitBias", v.logitBias);
	v.backendSampling = j.value("backendSampling", v.backendSampling);
	v.validate();
}

// ============================================================================
// UISettings serialization
// ============================================================================

void to_json(json &j, const UISettings &v)
{
	j = json{ { "theme", v.theme },
			  { "defaultTab", v.defaultTab },
			  { "showSystemPanel", v.showSystemPanel },
			  { "refreshRateMs", v.refreshRateMs },
			  { "logRetentionDays", v.logRetentionDays },
			  { "temperatureUnit", v.temperatureUnit },
			  { "cpuTemperatureGreenBottom", v.cpuTemperatureGreenBottom },
			  { "cpuTemperatureRedTop", v.cpuTemperatureRedTop },
			  { "gpuTemperatureGreenBottom", v.gpuTemperatureGreenBottom },
			  { "gpuTemperatureRedTop", v.gpuTemperatureRedTop },
			  { "systemResourcesOnly", v.systemResourcesOnly } };
}

void from_json(const json &j, UISettings &v)
{
	v.theme = j.value("theme", v.theme);
	v.defaultTab = j.value("defaultTab", v.defaultTab);
	v.showSystemPanel = j.value("showSystemPanel", v.showSystemPanel);
	v.refreshRateMs = j.value("refreshRateMs", v.refreshRateMs);
	v.logRetentionDays = j.value("logRetentionDays", v.logRetentionDays);
	v.temperatureUnit = j.value("temperatureUnit", v.temperatureUnit);
	v.cpuTemperatureGreenBottom =
		j.value("cpuTemperatureGreenBottom", v.cpuTemperatureGreenBottom);
	v.cpuTemperatureRedTop =
		j.value("cpuTemperatureRedTop", v.cpuTemperatureRedTop);
	v.gpuTemperatureGreenBottom =
		j.value("gpuTemperatureGreenBottom", v.gpuTemperatureGreenBottom);
	v.gpuTemperatureRedTop =
		j.value("gpuTemperatureRedTop", v.gpuTemperatureRedTop);
	v.systemResourcesOnly =
		j.value("systemResourcesOnly", v.systemResourcesOnly);

	// Backward-compat migration: old shared keys migrate to CPU fields only
	// when the new CPU fields still hold their defaults (30/80). GPU fields
	// always receive their own defaults (40/90) because GPU thresholds are a
	// new concept not present in old configs.
	if (j.contains("temperatureGreenBottom") &&
		v.cpuTemperatureGreenBottom == 30)
		v.cpuTemperatureGreenBottom = j.value("temperatureGreenBottom", 30);
	if (j.contains("temperatureRedTop") && v.cpuTemperatureRedTop == 80)
		v.cpuTemperatureRedTop = j.value("temperatureRedTop", 80);

	v.validate();
}

// ============================================================================
// TerminalSettings serialization
// ============================================================================

void to_json(json &j, const TerminalSettings &v)
{
	j["name"] = v.name;
	j["defaultShell"] = v.defaultShell;
	j["initialCommand"] = v.initialCommand;
	j["workingDirectory"] = v.workingDirectory;
	j["defaultCols"] = v.defaultCols;
	j["defaultRows"] = v.defaultRows;
}

void from_json(const json &j, TerminalSettings &v)
{
	v.name = j.value("name", v.name);
	v.defaultShell = j.value("defaultShell", v.defaultShell);
	v.initialCommand = j.value("initialCommand", v.initialCommand);
	v.workingDirectory = j.value("workingDirectory", v.workingDirectory);
	v.defaultCols = j.value("defaultCols", v.defaultCols);
	v.defaultRows = j.value("defaultRows", v.defaultRows);
	v.validate();
}

// ============================================================================
// ModelPreset serialization
// ============================================================================

void to_json(json &j, const ModelPreset &v)
{
	j["name"] = v.name;
	j["model"] = v.model;
	j["load"] = v.load;
	j["inference"] = v.inference;
}

void from_json(const json &j, ModelPreset &v)
{
	v.name = j.value("name", v.name);
	v.model = j.value("model", v.model);
	if (j.contains("load"))
		v.load = j["load"].get<LoadSettings>();
	if (j.contains("inference"))
		v.inference = j["inference"].get<InferenceSettings>();
	v.validate();
}

// ============================================================================
// TerminalPreset serialization
// ============================================================================

void to_json(json &j, const TerminalPreset &v)
{
	j["name"] = v.name;
	j["initialCommand"] = v.initialCommand;
	j["cols"] = v.cols;
	j["rows"] = v.rows;
}

void from_json(const json &j, TerminalPreset &v)
{
	v.name = j.value("name", std::string{});
	v.initialCommand = j.value("initialCommand", std::string{});
	v.cols = j.value("cols", 80);
	v.rows = j.value("rows", 24);
	v.validate();
}

// ============================================================================
// DiscoverySettings serialization
// ============================================================================

void to_json(json &j, const DiscoverySettings &v)
{
	j["modelSearchPath"] = v.modelSearchPath;
	j["fileFilter"] = v.fileFilter;
}

void from_json(const json &j, DiscoverySettings &v)
{
	v.modelSearchPath = j.value("modelSearchPath", std::string{});
	v.fileFilter = j.value("fileFilter", std::vector<std::string>{ "mmproj*" });
	v.validate();
}

// ============================================================================
// VllmSettings serialization
// ============================================================================

void to_json(json &j, const VllmSettings &v)
{
	j = json{ { "host", v.host }, { "port", v.port } };
}

void from_json(const json &j, VllmSettings &v)
{
	v.host = j.value("host", v.host);
	v.port = j.value("port", v.port);
	v.validate();
}

// ============================================================================
// UserConfig serialization (main container)
// ============================================================================

void to_json(json &j, const UserConfig &v)
{
	j["server"] = v.server;
	j["api"] = v.api;
	j["ui"] = v.ui;
	j["terminal"] = v.terminal;
	j["discovery"] = v.discovery;
	j["terminalPresets"] = v.terminalPresets;
	j["vllm"] = v.vllm;
}

void from_json(const json &j, UserConfig &v)
{
	if (j.contains("server"))
		v.server = j["server"].get<ServerSettings>();
	if (j.contains("api"))
		v.api = j["api"].get<ApiSettings>();
	// "load"/"inference"/"presets" are legacy top-level keys (model presets now
	// live in models.ini). Silently ignored if present in an older config.json.
	if (j.contains("ui"))
		v.ui = j["ui"].get<UISettings>();
	if (j.contains("terminal"))
		v.terminal = j["terminal"].get<TerminalSettings>();
	if (j.contains("discovery"))
		v.discovery = j["discovery"].get<DiscoverySettings>();
	if (j.contains("terminalPresets"))
		v.terminalPresets =
			j["terminalPresets"].get<std::vector<TerminalPreset>>();
	if (j.contains("vllm"))
		v.vllm = j["vllm"].get<VllmSettings>();
	v.validateAll();
}

// ============================================================================
// Validation
// ============================================================================

void LoadSettings::validate() noexcept
{
	ngpuLayers = std::clamp(ngpuLayers, -1, 99);

	// ctxSize is NOT rounded or upper-clamped: llama.cpp accepts any context
	// length, and the model's true maximum is unknown here, so we must not
	// second-guess the user's value. 0 means "use the model's default"; a
	// negative value is nonsensical, so floor at 0 (i.e. treat as default).
	if (ctxSize < 0)
		ctxSize = 0;

	batchSize = std::clamp(batchSize, 1, 65536);
	ubatchSize = std::clamp(ubatchSize, 1, 4096);
	parallel = std::clamp(parallel, 1, 256);
	threads = std::clamp(threads, -1, 1024);
	threadsBatch = std::clamp(threadsBatch, -1, 1024);
	draftMax = std::clamp(draftMax, -1, 256);

	static const std::array<std::string_view, 10> validSpecTypes = {
		"none",			"draft-simple", "draft-eagle3", "draft-mtp",
		"draft-dflash", "ngram-simple", "ngram-map-k",	"ngram-map-k4v",
		"ngram-mod",	"ngram-cache"
	};
	if (!specType.empty()) {
		std::istringstream ss(specType);
		std::string tok;
		bool allValid = true;
		while (std::getline(ss, tok, ',')) {
			if (!std::ranges::any_of(validSpecTypes, [&](std::string_view t) {
					return tok == t;
				})) {
				allValid = false;
				break;
			}
		}
		if (!allValid)
			specType.clear();
	}

	// Sanitize tensorSplit: split on ',', validate each token as non-negative
	// decimal, round to nearest hundredth, rejoin. Preserves empty string as-is.
	// Rounding logic must mirror ModelsPanel::roundToHundredth() exactly.
	if (!tensorSplit.empty()) {
		try {
			std::vector<std::string> tokens;
			std::istringstream tss(tensorSplit);
			std::string tok;
			while (std::getline(tss, tok, ','))
				tokens.push_back(tok);

			std::string sanitized;
			for (size_t i = 0; i < tokens.size(); ++i) {
				std::istringstream iss(tokens[i]);
				iss.imbue(std::locale::classic());
				double v = 0.0;
				double result = 1.0;
				if ((iss >> v) && v >= 0.0)
					result = std::round(v * 100.0) / 100.0;
				std::ostringstream oss;
				oss.imbue(std::locale::classic());
				oss << std::fixed << std::setprecision(2) << result;
				if (i > 0)
					sanitized += ',';
				sanitized += oss.str();
			}
			tensorSplit = sanitized;
		} catch (...) {
			tensorSplit.clear();
		}
	}

	static constexpr std::array<std::string_view, 9> validCacheTypes = {
		"f32", "f16", "bf16", "q8_0", "q4_0", "q4_1", "iq4_nl", "q5_0", "q5_1"
	};
	if (!std::ranges::any_of(validCacheTypes, [&](std::string_view t) {
			return cacheTypeK == t;
		}))
		cacheTypeK = "f16";
	if (!std::ranges::any_of(validCacheTypes, [&](std::string_view t) {
			return cacheTypeV == t;
		}))
		cacheTypeV = "f16";
	if (!std::ranges::any_of(validCacheTypes, [&](std::string_view t) {
			return cacheTypeKDraft == t;
		}))
		cacheTypeKDraft = "f16";
	if (!std::ranges::any_of(validCacheTypes, [&](std::string_view t) {
			return cacheTypeVDraft == t;
		}))
		cacheTypeVDraft = "f16";

	// --- Issue #103 section C ---
	nCpuMoe = std::clamp(nCpuMoe, -1, 4096);
	yarnOrigCtx = std::clamp(yarnOrigCtx, 0, 1048576);
	keep = std::clamp(keep, -1, 1048576);
	fitCtx = std::clamp(fitCtx, 0, 1048576);
	specDraftNMin = std::clamp(specDraftNMin, -1, 4096);

	// Enum whitelists — invalid resets to "" (mirrors specType handling).
	static constexpr std::array<std::string_view, 3> validRopeScaling = {
		"none",
		"linear",
		"yarn"
	};
	if (!ropeScaling.empty() &&
		!std::ranges::any_of(validRopeScaling, [&](std::string_view t) {
			return ropeScaling == t;
		}))
		ropeScaling.clear();

	static constexpr std::array<std::string_view, 3> validNuma = { "distribute",
																   "isolate",
																   "numactl" };
	if (!numa.empty() &&
		!std::ranges::any_of(validNuma,
							 [&](std::string_view t) { return numa == t; }))
		numa.clear();

	static constexpr std::array<std::string_view, 3> validFitTarget = { "auto",
																		"vram",
																		"ram" };
	if (!fitTarget.empty() &&
		!std::ranges::any_of(validFitTarget,
							 [&](std::string_view t) { return fitTarget == t; }))
		fitTarget.clear();
}

void InferenceSettings::validate() noexcept
{
	nPredict = std::clamp(nPredict, -1, 65536);
	temperature = std::clamp(temperature, 0.0, 2.0);
	topK = std::clamp(topK, 0, 4096);
	topP = std::clamp(topP, 0.0, 1.0);
	minP = std::clamp(minP, 0.0, 1.0);
	typicalP = std::clamp(typicalP, 0.0, 1.0);
	topNsigma = std::clamp(topNsigma, -1.0, 4.0);
	xtcProbability = std::clamp(xtcProbability, 0.0, 1.0);
	xtcThreshold = std::clamp(xtcThreshold, 0.0, 1.0);
	repeatLastN = std::clamp(repeatLastN, -1, 65536);
	repeatPenalty = std::clamp(repeatPenalty, 0.5, 3.0);
	presencePenalty = std::clamp(presencePenalty, -2.0, 2.0);
	frequencyPenalty = std::clamp(frequencyPenalty, -2.0, 2.0);
	dryMultiplier = std::clamp(dryMultiplier, 0.0, 10.0);
	dryBase = std::clamp(dryBase, 1.0, 5.0);
	dryAllowedLength = std::clamp(dryAllowedLength, 1, 64);
	dryPenaltyLastN = std::clamp(dryPenaltyLastN, -1, 65536);
	dynatempRange = std::clamp(dynatempRange, 0.0, 2.0);
	dynatempExp = std::clamp(dynatempExp, 0.1, 5.0);
	mirostat = std::clamp(mirostat, 0, 2);
	mirostatLr = std::clamp(mirostatLr, 0.01, 1.0);
	mirostatEnt = std::clamp(mirostatEnt, 1.0, 10.0);

	adaptiveTarget = std::clamp(adaptiveTarget, 0.0, 1024.0);
	adaptiveDecay = std::clamp(adaptiveDecay, 0.0, 1.0);
}

void ApiSettings::validate() noexcept
{
	apiPort = std::clamp(apiPort, 1, 65535);
}

void VllmSettings::validate() noexcept
{
	port = std::clamp(port, 1, 65535);
}

void ServerSettings::validate()
{
	port = std::clamp(port, 1, 65535);
	timeout = std::clamp(timeout, 1, 86400);
	threadsHttp = std::clamp(threadsHttp, -1, 256);
	cacheReuse = std::clamp(cacheReuse, 0, 4096);
	cacheRam = std::clamp(cacheRam, 0, 1048576);
	ctxCheckpoints = std::clamp(ctxCheckpoints, 0, 256);
	slotPromptSimilarity = std::clamp(slotPromptSimilarity, 0.0, 1.0);
	sleepIdleSeconds = std::clamp(sleepIdleSeconds, -1, 86400);

	modelsMax = std::clamp(modelsMax, 1, 64);
	checkpointMinStep = std::clamp(checkpointMinStep, 0, 1048576);
	// -1 means "omit" (server default); otherwise a sane upper bound.
	if (embdNormalize != -1)
		embdNormalize = std::clamp(embdNormalize, 0, 2);
	if (reasoningBudget != -1)
		reasoningBudget = std::clamp(reasoningBudget, 0, 1048576);

	// Enum whitelists — invalid values reset to default (mirrors LoadSettings).
	static const std::array<std::string_view, 5> validPooling = { "none",
																  "mean",
																  "cls",
																  "last",
																  "rank" };
	if (!pooling.empty() &&
		!std::ranges::any_of(validPooling,
							 [&](std::string_view t) { return pooling == t; }))
		pooling.clear();

	static const std::array<std::string_view, 3> validReasoning = { "on",
																	"off",
																	"auto" };
	if (!std::ranges::any_of(validReasoning,
							 [&](std::string_view t) { return reasoning == t; }))
		reasoning = "auto";

	// Cross-field: derive sslKeyFile when cert is set but key is missing
	if (!sslCertFile.empty() && sslKeyFile.empty())
		sslKeyFile = sslCertFile + ".key";
}

void UISettings::validate() noexcept
{
	defaultTab = std::clamp(defaultTab, 0, 10);
	refreshRateMs = std::clamp(refreshRateMs, 20, 5000);
	logRetentionDays = std::clamp(logRetentionDays, 0, 365);

	static const std::array<std::string_view, 2> validTemperatureUnit = {
		"celsius",
		"fahrenheit"
	};
	if (!std::ranges::any_of(validTemperatureUnit, [&](std::string_view t) {
			return temperatureUnit == t;
		}))
		temperatureUnit = "celsius";

	// Temperature thresholds: clamp each pair to realistic range, swap if
	// inverted. CPU and GPU pairs are validated independently — clamping one
	// pair never affects the other.
	// Only swap when BOTH values are already within range — if one was
	// clamped to a boundary the user clearly intended that extreme value,
	// and swapping would undo the clamping.
	{
		// CPU thresholds
		const int origCpuGreen = cpuTemperatureGreenBottom;
		const int origCpuRed = cpuTemperatureRedTop;
		cpuTemperatureGreenBottom =
			std::clamp(cpuTemperatureGreenBottom, -50, 200);
		cpuTemperatureRedTop = std::clamp(cpuTemperatureRedTop, -50, 200);
		const bool cpuGreenClamped = origCpuGreen != cpuTemperatureGreenBottom;
		const bool cpuRedClamped = origCpuRed != cpuTemperatureRedTop;
		if (!cpuGreenClamped && !cpuRedClamped &&
			cpuTemperatureGreenBottom > cpuTemperatureRedTop)
			std::swap(cpuTemperatureGreenBottom, cpuTemperatureRedTop);

		// GPU thresholds
		const int origGpuGreen = gpuTemperatureGreenBottom;
		const int origGpuRed = gpuTemperatureRedTop;
		gpuTemperatureGreenBottom =
			std::clamp(gpuTemperatureGreenBottom, -50, 200);
		gpuTemperatureRedTop = std::clamp(gpuTemperatureRedTop, -50, 200);
		const bool gpuGreenClamped = origGpuGreen != gpuTemperatureGreenBottom;
		const bool gpuRedClamped = origGpuRed != gpuTemperatureRedTop;
		if (!gpuGreenClamped && !gpuRedClamped &&
			gpuTemperatureGreenBottom > gpuTemperatureRedTop)
			std::swap(gpuTemperatureGreenBottom, gpuTemperatureRedTop);
	}
}

void ModelPreset::validate()
{
	if (name.empty())
		name = "Unnamed";
	// Nested load/inference validated via from_json chain
}

void TerminalPreset::validate()
{
	if (name.empty())
		name = "Unnamed";
	cols = std::clamp(cols, 16, 4096);
	rows = std::clamp(rows, 8, 4096);
}

void TerminalSettings::validate()
{
	if (name.empty())
		name = "Unnamed";
	defaultCols = std::clamp(defaultCols, 16, 4096);
	defaultRows = std::clamp(defaultRows, 8, 4096);
}

void UserConfig::validateAll()
{
	server.validate();
	api.validate();
	ui.validate();
	terminal.validate();
	discovery.validate();
	vllm.validate();

	for (auto &tp : terminalPresets)
		tp.validate();
}

} // namespace Config
