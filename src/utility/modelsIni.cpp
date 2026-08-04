#include "modelsIni.h"
#include "configManager.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <spdlog/spdlog.h>
#include <sstream>

namespace {
/**
 * @brief Trim whitespace from string
 */
std::string trim(const std::string &str)
{
	size_t start = str.find_first_not_of(" \t\r\n");
	if (start == std::string::npos)
		return "";
	size_t end = str.find_last_not_of(" \t\r\n");
	return str.substr(start, end - start + 1);
}

/**
 * @brief Check if line is a comment or empty
 */
bool isCommentOrEmpty(const std::string &line)
{
	std::string trimmed = trim(line);
	return trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#';
}
} // namespace

ModelsIni &ModelsIni::instance()
{
	static ModelsIni instance;
	return instance;
}

std::string ModelsIni::getPath() const
{
	return m_filePath;
}

bool ModelsIni::load()
{
	std::string configDir = ConfigManager::instance().getConfigDir();
	m_filePath = configDir + "/models.ini";
	spdlog::debug("Loading models.ini from: {}", m_filePath);

	std::ifstream file(m_filePath);
	if (!file.is_open()) {
		spdlog::warn("models.ini not found, will create default");
		return false;
	}

	m_sections.clear();
	std::string currentSection;
	std::string line;

	while (std::getline(file, line)) {
		// Skip comments and empty lines
		if (isCommentOrEmpty(line))
			continue;

		// Check for section header: [section_name]. Trim first so an
		// indented header parses the same way findSectionRange() sees it
		// (which trims) — untrimmed, "  [foo]" fell through to the key=value
		// branch and its keys landed in the previous section.
		std::string trimmedLine = trim(line);
		if (trimmedLine[0] == '[') {
			size_t end = trimmedLine.find(']');
			if (end != std::string::npos) {
				currentSection = trim(trimmedLine.substr(1, end - 1));
				// Create empty section if it doesn't exist
				if (m_sections.find(currentSection) == m_sections.end()) {
					m_sections[currentSection] = {};
				}
			}
			continue;
		}

		// Parse key = value
		size_t equalsPos = line.find('=');
		if (equalsPos != std::string::npos && !currentSection.empty()) {
			std::string key = trim(line.substr(0, equalsPos));
			std::string value = trim(line.substr(equalsPos + 1));
			m_sections[currentSection][key] = value;
		}
	}

	spdlog::info("Loaded {} model(s) from models.ini", getModelNames().size());
	return true;
}

bool ModelsIni::createDefault()
{
	std::string configDir = ConfigManager::instance().getConfigDir();
	m_filePath = configDir + "/models.ini";

	std::ofstream file(m_filePath);
	if (!file.is_open()) {
		spdlog::error("Failed to create models.ini at: {}", m_filePath);
		return false;
	}

	file << "version = 1\n";
	file << "\n";
	file << "; Global defaults for all models\n";
	file << "[*]\n";
	file << "; Add global defaults here, e.g.:\n";
	file << "; c = 4096\n";
	file << "; n-gpu-layers = 99\n";
	file << "\n";
	file << "; Example model entries:\n";
	file << ";\n";
	file << "; [orchestrator]\n";
	file << "; model = "
			"D:/Models/bartowski/nvidia_Orchestrator-8B-GGUF/"
			"nvidia_Orchestrator-8B-Q6_K_L.gguf\n";
	file << ";\n";
	file << "; [qwen-27b]\n";
	file << "; model = "
			"D:/Models/bartowski/Qwen_Qwen3.5-27B-GGUF/"
			"Qwen_Qwen3.5-27B-Q4_K_M.gguf\n";

	file.close();

	spdlog::info("Created default models.ini at: {}", m_filePath);

	// Also load the newly created file
	return load();
}

std::vector<std::string> ModelsIni::getModelNames() const
{
	std::vector<std::string> names;
	for (const auto &section : m_sections) {
		// Skip the global [*] section
		if (section.first != "*") {
			names.push_back(section.first);
		}
	}
	return names;
}

std::string ModelsIni::getModelPath(const std::string &sectionName) const
{
	auto sectionIt = m_sections.find(sectionName);
	if (sectionIt == m_sections.end()) {
		return "";
	}

	auto pathIt = sectionIt->second.find("model");
	if (pathIt == sectionIt->second.end()) {
		return "";
	}

	return pathIt->second;
}

std::map<std::string, std::string>
ModelsIni::getSectionValues(const std::string &sectionName) const
{
	auto sectionIt = m_sections.find(sectionName);
	if (sectionIt == m_sections.end()) {
		return {};
	}
	return sectionIt->second;
}

std::map<std::string, std::string> ModelsIni::getGlobalDefaults() const
{
	auto globalIt = m_sections.find("*");
	if (globalIt == m_sections.end()) {
		return {};
	}
	return globalIt->second;
}

// =========================================================================
// Serialisation helpers (INI key <-> struct)
// =========================================================================

namespace {

/// Helper: convert string to bool ("true"/"on"/"1" → true)
bool toBool(const std::string &v, bool fallback)
{
	if (v.empty())
		return fallback;
	std::string lv = v;
	std::transform(lv.begin(), lv.end(), lv.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	if (lv == "true" || lv == "on" || lv == "1")
		return true;
	if (lv == "false" || lv == "off" || lv == "0")
		return false;
	return fallback;
}

std::string fromBool(bool v)
{
	return v ? "true" : "false";
}

int toInt(const std::string &v, int fallback)
{
	try {
		return std::stoi(v);
	} catch (...) {
		return fallback;
	}
}

double toDouble(const std::string &v, double fallback)
{
	try {
		return std::stod(v);
	} catch (...) {
		return fallback;
	}
}

/// Try two keys (primary kebab-case, fallback underscore/abbreviated)
std::string getAny(const std::map<std::string, std::string> &kv,
				   const std::string &k1,
				   const std::string &k2 = "")
{
	auto it = kv.find(k1);
	if (it != kv.end())
		return it->second;
	if (!k2.empty()) {
		it = kv.find(k2);
		if (it != kv.end())
			return it->second;
	}
	return "";
}

/// Build LoadSettings from INI key-value pairs
/// Accepts both kebab-case (existing format) and abbreviated keys
Config::LoadSettings
iniToLoadSettings(const std::map<std::string, std::string> &kv)
{
	Config::LoadSettings s;

	auto v = getAny(kv, "n-gpu-layers", "ngl");
	if (!v.empty())
		s.ngpuLayers = toInt(v, s.ngpuLayers);
	v = getAny(kv, "ctx-size", "ctx");
	if (!v.empty())
		s.ctxSize = toInt(v, s.ctxSize);
	v = getAny(kv, "batch-size", "batch");
	if (!v.empty())
		s.batchSize = toInt(v, s.batchSize);
	v = getAny(kv, "ubatch-size", "ubatch");
	if (!v.empty())
		s.ubatchSize = toInt(v, s.ubatchSize);
	v = getAny(kv, "parallel");
	if (!v.empty())
		s.parallel = toInt(v, s.parallel);
	v = getAny(kv, "flash-attn", "flash_attn");
	if (!v.empty())
		s.flashAttn = v;
	v = getAny(kv, "kv-offload", "kv_offload");
	if (!v.empty())
		s.kvOffload = toBool(v, s.kvOffload);
	v = getAny(kv, "kv-unified", "kv_unified");
	if (!v.empty())
		s.kvUnified = toBool(v, s.kvUnified);
	v = getAny(kv, "mmap");
	if (!v.empty())
		s.mmap = toBool(v, s.mmap);
	v = getAny(kv, "mlock");
	if (!v.empty())
		s.mlock = toBool(v, s.mlock);
	v = getAny(kv, "fit");
	if (!v.empty())
		s.fit = toBool(v, s.fit);
	v = getAny(kv, "split-mode", "split_mode");
	if (!v.empty())
		s.splitMode = v;
	v = getAny(kv, "tensor-split", "tensor_split");
	if (!v.empty())
		s.tensorSplit = v;
	v = getAny(kv, "device-priority", "device_priority");
	if (!v.empty())
		s.devicePriority = v;
	v = getAny(kv, "cache-type-k", "cache_type_k");
	if (!v.empty())
		s.cacheTypeK = v;
	v = getAny(kv, "cache-type-v", "cache_type_v");
	if (!v.empty())
		s.cacheTypeV = v;
	v = getAny(kv, "lora");
	if (!v.empty())
		s.lora = v;
	v = getAny(kv, "mmproj");
	if (!v.empty())
		s.mmproj = v;
	v = getAny(kv, "model-draft", "model_draft");
	if (!v.empty())
		s.modelDraft = v;
	v = getAny(kv, "spec-draft-n-max", "spec_draft_n_max");
	if (!v.empty())
		s.draftMax = toInt(v, s.draftMax);
	v = getAny(kv, "chat-template", "chat_template");
	if (!v.empty())
		s.chatTemplate = v;
	v = getAny(kv, "reasoning-format", "reasoning_format");
	if (!v.empty())
		s.reasoningFormat = v;
	v = getAny(kv, "spec-type", "spec_type");
	if (!v.empty())
		s.specType = v;
	v = getAny(kv, "cache-type-k-draft", "cache_type_k_draft");
	if (!v.empty())
		s.cacheTypeKDraft = v;
	v = getAny(kv, "cache-type-v-draft", "cache_type_v_draft");
	if (!v.empty())
		s.cacheTypeVDraft = v;
	v = getAny(kv, "device-draft", "device_draft");
	if (!v.empty())
		s.deviceDraft = v;
	v = getAny(kv, "chat-template-kwargs", "chat_template_kwargs");
	if (!v.empty())
		s.preserveThinking = (v.find("preserve_thinking") != std::string::npos &&
							  v.find("true") != std::string::npos);
	v = getAny(kv, "threads");
	if (!v.empty())
		s.threads = toInt(v, s.threads);
	v = getAny(kv, "threads-batch");
	if (!v.empty())
		s.threadsBatch = toInt(v, s.threadsBatch);

	// --- Issue #103 section C ---
	v = getAny(kv, "n-cpu-moe", "ncmoe");
	if (!v.empty())
		s.nCpuMoe = toInt(v, s.nCpuMoe);
	v = getAny(kv, "cpu-moe", "cpu_moe");
	if (!v.empty())
		s.cpuMoe = toBool(v, s.cpuMoe);
	v = getAny(kv, "override-tensor", "ot");
	if (!v.empty())
		s.overrideTensor = v;
	v = getAny(kv, "rope-scaling", "rope_scaling");
	if (!v.empty())
		s.ropeScaling = v;
	v = getAny(kv, "rope-scale", "rope_scale");
	if (!v.empty())
		s.ropeScale = toDouble(v, s.ropeScale);
	v = getAny(kv, "rope-freq-base", "rope_freq_base");
	if (!v.empty())
		s.ropeFreqBase = toDouble(v, s.ropeFreqBase);
	v = getAny(kv, "rope-freq-scale", "rope_freq_scale");
	if (!v.empty())
		s.ropeFreqScale = toDouble(v, s.ropeFreqScale);
	v = getAny(kv, "yarn-orig-ctx", "yarn_orig_ctx");
	if (!v.empty())
		s.yarnOrigCtx = toInt(v, s.yarnOrigCtx);
	v = getAny(kv, "yarn-ext-factor", "yarn_ext_factor");
	if (!v.empty())
		s.yarnExtFactor = toDouble(v, s.yarnExtFactor);
	v = getAny(kv, "yarn-attn-factor", "yarn_attn_factor");
	if (!v.empty())
		s.yarnAttnFactor = toDouble(v, s.yarnAttnFactor);
	v = getAny(kv, "yarn-beta-slow", "yarn_beta_slow");
	if (!v.empty())
		s.yarnBetaSlow = toDouble(v, s.yarnBetaSlow);
	v = getAny(kv, "yarn-beta-fast", "yarn_beta_fast");
	if (!v.empty())
		s.yarnBetaFast = toDouble(v, s.yarnBetaFast);
	v = getAny(kv, "swa-full", "swa_full");
	if (!v.empty())
		s.swaFull = toBool(v, s.swaFull);
	v = getAny(kv, "keep");
	if (!v.empty())
		s.keep = toInt(v, s.keep);
	v = getAny(kv, "numa");
	if (!v.empty())
		s.numa = v;
	v = getAny(kv, "fit-target", "fit_target");
	if (!v.empty())
		s.fitTarget = v;
	v = getAny(kv, "fit-ctx", "fit_ctx");
	if (!v.empty())
		s.fitCtx = toInt(v, s.fitCtx);
	v = getAny(kv, "check-tensors", "check_tensors");
	if (!v.empty())
		s.checkTensors = toBool(v, s.checkTensors);
	v = getAny(kv, "override-kv", "override_kv");
	if (!v.empty())
		s.overrideKv = v;
	v = getAny(kv, "lora-scaled", "lora_scaled");
	if (!v.empty())
		s.loraScaled = v;
	v = getAny(kv, "control-vector", "control_vector");
	if (!v.empty())
		s.controlVector = v;
	v = getAny(kv, "control-vector-scaled", "control_vector_scaled");
	if (!v.empty())
		s.controlVectorScaled = v;
	v = getAny(kv, "spec-draft-n-min", "spec_draft_n_min");
	if (!v.empty())
		s.specDraftNMin = toInt(v, s.specDraftNMin);
	v = getAny(kv, "spec-draft-p-min", "spec_draft_p_min");
	if (!v.empty())
		s.specDraftPMin = toDouble(v, s.specDraftPMin);
	v = getAny(kv, "spec-draft-p-split", "spec_draft_p_split");
	if (!v.empty())
		s.specDraftPSplit = toDouble(v, s.specDraftPSplit);
	v = getAny(kv, "cpu-moe-draft", "cpu_moe_draft");
	if (!v.empty())
		s.cpuMoeDraft = toBool(v, s.cpuMoeDraft);

	return s;
}

/// Build InferenceSettings from INI key-value pairs (keys already stripped of
/// "inf." prefix if present). Accepts both kebab-case and underscore keys.
Config::InferenceSettings
iniToInferenceSettings(const std::map<std::string, std::string> &kv)
{
	Config::InferenceSettings s;

	auto v = getAny(kv, "temp", "temperature");
	if (!v.empty())
		s.temperature = toDouble(v, s.temperature);
	v = getAny(kv, "top-p", "top_p");
	if (!v.empty())
		s.topP = toDouble(v, s.topP);
	v = getAny(kv, "top-k", "top_k");
	if (!v.empty())
		s.topK = toInt(v, s.topK);
	v = getAny(kv, "min-p", "min_p");
	if (!v.empty())
		s.minP = toDouble(v, s.minP);
	v = getAny(kv, "repeat-penalty", "repeat_penalty");
	if (!v.empty())
		s.repeatPenalty = toDouble(v, s.repeatPenalty);
	v = getAny(kv, "presence-penalty", "presence_penalty");
	if (!v.empty())
		s.presencePenalty = toDouble(v, s.presencePenalty);
	v = getAny(kv, "frequency-penalty", "frequency_penalty");
	if (!v.empty())
		s.frequencyPenalty = toDouble(v, s.frequencyPenalty);
	v = getAny(kv, "n-predict", "n_predict");
	if (!v.empty())
		s.nPredict = toInt(v, s.nPredict);
	v = getAny(kv, "seed");
	if (!v.empty())
		s.seed = toInt(v, s.seed);
	v = getAny(kv, "samplers");
	if (!v.empty())
		s.samplers = v;
	v = getAny(kv, "repeat-last-n");
	if (!v.empty())
		s.repeatLastN = toInt(v, s.repeatLastN);
	v = getAny(kv, "ignore-eos");
	if (!v.empty())
		s.ignoreEos = toBool(v, s.ignoreEos);
	v = getAny(kv, "logit-bias");
	if (!v.empty())
		s.logitBias = v;
	v = getAny(kv, "adaptive-target");
	if (!v.empty())
		s.adaptiveTarget = toDouble(v, s.adaptiveTarget);
	v = getAny(kv, "adaptive-decay");
	if (!v.empty())
		s.adaptiveDecay = toDouble(v, s.adaptiveDecay);
	v = getAny(kv, "grammar-file", "grammar_file");
	if (!v.empty())
		s.grammarFile = v;
	v = getAny(kv, "json-schema-file", "json_schema_file");
	if (!v.empty())
		s.jsonSchemaFile = v;
	v = getAny(kv, "sampler-seq", "sampler_seq");
	if (!v.empty())
		s.samplerSeq = v;
	v = getAny(kv, "dry-sequence-breaker", "dry_sequence_breaker");
	if (!v.empty())
		s.drySequenceBreaker = v;
	v = getAny(kv, "backend-sampling", "backend_sampling");
	if (!v.empty())
		s.backendSampling = toBool(v, s.backendSampling);

	return s;
}

/// Convert LoadSettings to INI key-value pairs (kebab-case, matching
/// llama-server CLI format). Returns lines as ordered vector to control output
/// order.
std::vector<std::pair<std::string, std::string>>
loadSettingsToIni(const Config::LoadSettings &s)
{
	std::vector<std::pair<std::string, std::string>> kv;
	kv.emplace_back("batch-size", std::to_string(s.batchSize));
	kv.emplace_back("ubatch-size", std::to_string(s.ubatchSize));
	kv.emplace_back("ctx-size", std::to_string(s.ctxSize));
	kv.emplace_back("parallel", std::to_string(s.parallel));
	kv.emplace_back("n-gpu-layers",
					s.ngpuLayers < 0 ? "all" : std::to_string(s.ngpuLayers));
	kv.emplace_back("flash-attn", s.flashAttn.empty() ? "on" : s.flashAttn);
	kv.emplace_back("fit", s.fit ? "on" : "off");
	kv.emplace_back("mmap", s.mmap ? "1" : "0");
	kv.emplace_back("mlock", s.mlock ? "1" : "0");
	kv.emplace_back("kv-offload", s.kvOffload ? "1" : "0");
	kv.emplace_back("kv-unified", s.kvUnified ? "1" : "0");
	kv.emplace_back("cache-type-k", s.cacheTypeK);
	kv.emplace_back("cache-type-v", s.cacheTypeV);
	kv.emplace_back("threads", std::to_string(s.threads));
	kv.emplace_back("threads-batch", std::to_string(s.threadsBatch));
	if (!s.splitMode.empty())
		kv.emplace_back("split-mode", s.splitMode);
	if (!s.tensorSplit.empty())
		kv.emplace_back("tensor-split", s.tensorSplit);
	if (!s.devicePriority.empty())
		kv.emplace_back("device-priority", s.devicePriority);
	if (!s.lora.empty())
		kv.emplace_back("lora", s.lora);
	if (!s.mmproj.empty())
		kv.emplace_back("mmproj", s.mmproj);
	if (!s.modelDraft.empty())
		kv.emplace_back("model-draft", s.modelDraft);
	if (s.draftMax != -1)
		kv.emplace_back("spec-draft-n-max", std::to_string(s.draftMax));
	if (!s.chatTemplate.empty())
		kv.emplace_back("chat-template", s.chatTemplate);
	if (!s.reasoningFormat.empty())
		kv.emplace_back("reasoning-format", s.reasoningFormat);
	if (!s.specType.empty())
		kv.emplace_back("spec-type", s.specType);
	if (!s.cacheTypeKDraft.empty() && s.cacheTypeKDraft != "f16")
		kv.emplace_back("cache-type-k-draft", s.cacheTypeKDraft);
	if (!s.cacheTypeVDraft.empty() && s.cacheTypeVDraft != "f16")
		kv.emplace_back("cache-type-v-draft", s.cacheTypeVDraft);
	if (!s.deviceDraft.empty())
		kv.emplace_back("device-draft", s.deviceDraft);
	// TODO(#87): single-key only; promote to free-form chatTemplateKwargs if
	// more keys needed.
	if (s.preserveThinking)
		kv.emplace_back("chat-template-kwargs",
						R"({"preserve_thinking": true})");

	// --- Issue #103 section C (emit only when non-default) ---
	auto fmt = [](double v) {
		std::ostringstream os;
		os << v;
		return os.str();
	};
	if (s.nCpuMoe != -1)
		kv.emplace_back("n-cpu-moe", std::to_string(s.nCpuMoe));
	if (s.cpuMoe)
		kv.emplace_back("cpu-moe", "true");
	if (!s.overrideTensor.empty())
		kv.emplace_back("override-tensor", s.overrideTensor);
	if (!s.ropeScaling.empty())
		kv.emplace_back("rope-scaling", s.ropeScaling);
	if (s.ropeScale != 0.0)
		kv.emplace_back("rope-scale", fmt(s.ropeScale));
	if (s.ropeFreqBase != 0.0)
		kv.emplace_back("rope-freq-base", fmt(s.ropeFreqBase));
	if (s.ropeFreqScale != 0.0)
		kv.emplace_back("rope-freq-scale", fmt(s.ropeFreqScale));
	if (s.yarnOrigCtx != 0)
		kv.emplace_back("yarn-orig-ctx", std::to_string(s.yarnOrigCtx));
	if (s.yarnExtFactor != -1.0)
		kv.emplace_back("yarn-ext-factor", fmt(s.yarnExtFactor));
	if (s.yarnAttnFactor != 1.0)
		kv.emplace_back("yarn-attn-factor", fmt(s.yarnAttnFactor));
	if (s.yarnBetaSlow != 1.0)
		kv.emplace_back("yarn-beta-slow", fmt(s.yarnBetaSlow));
	if (s.yarnBetaFast != 32.0)
		kv.emplace_back("yarn-beta-fast", fmt(s.yarnBetaFast));
	if (s.swaFull)
		kv.emplace_back("swa-full", "true");
	if (s.keep != -1)
		kv.emplace_back("keep", std::to_string(s.keep));
	if (!s.numa.empty())
		kv.emplace_back("numa", s.numa);
	if (!s.fitTarget.empty())
		kv.emplace_back("fit-target", s.fitTarget);
	if (s.fitCtx != 0)
		kv.emplace_back("fit-ctx", std::to_string(s.fitCtx));
	if (s.checkTensors)
		kv.emplace_back("check-tensors", "true");
	if (!s.overrideKv.empty())
		kv.emplace_back("override-kv", s.overrideKv);
	if (!s.loraScaled.empty())
		kv.emplace_back("lora-scaled", s.loraScaled);
	if (!s.controlVector.empty())
		kv.emplace_back("control-vector", s.controlVector);
	if (!s.controlVectorScaled.empty())
		kv.emplace_back("control-vector-scaled", s.controlVectorScaled);
	if (s.specDraftNMin != -1)
		kv.emplace_back("spec-draft-n-min", std::to_string(s.specDraftNMin));
	if (s.specDraftPMin != -1.0)
		kv.emplace_back("spec-draft-p-min", fmt(s.specDraftPMin));
	if (s.specDraftPSplit != -1.0)
		kv.emplace_back("spec-draft-p-split", fmt(s.specDraftPSplit));
	if (s.cpuMoeDraft)
		kv.emplace_back("cpu-moe-draft", "true");

	return kv;
}

/// Convert InferenceSettings to INI key-value pairs (kebab-case, no prefix)
std::vector<std::pair<std::string, std::string>>
inferenceSettingsToIni(const Config::InferenceSettings &s)
{
	auto fmt = [](double v) {
		std::ostringstream os;
		os << v;
		return os.str();
	};
	std::vector<std::pair<std::string, std::string>> kv;
	kv.emplace_back("temp", fmt(s.temperature));
	kv.emplace_back("top-p", fmt(s.topP));
	kv.emplace_back("top-k", std::to_string(s.topK));
	kv.emplace_back("min-p", fmt(s.minP));
	kv.emplace_back("repeat-penalty", fmt(s.repeatPenalty));
	kv.emplace_back("repeat-last-n", std::to_string(s.repeatLastN));
	kv.emplace_back("presence-penalty", fmt(s.presencePenalty));
	kv.emplace_back("frequency-penalty", fmt(s.frequencyPenalty));
	kv.emplace_back("seed", std::to_string(s.seed));
	kv.emplace_back("n-predict", std::to_string(s.nPredict));
	if (!s.samplers.empty())
		kv.emplace_back("samplers", s.samplers);
	if (s.ignoreEos)
		kv.emplace_back("ignore-eos", "true");
	if (!s.logitBias.empty())
		kv.emplace_back("logit-bias", s.logitBias);
	if (s.adaptiveTarget != 0.0)
		kv.emplace_back("adaptive-target", fmt(s.adaptiveTarget));
	if (s.adaptiveDecay != 0.0)
		kv.emplace_back("adaptive-decay", fmt(s.adaptiveDecay));
	if (!s.jsonSchemaFile.empty())
		kv.emplace_back("json-schema-file", s.jsonSchemaFile);
	if (!s.grammarFile.empty())
		kv.emplace_back("grammar-file", s.grammarFile);
	if (!s.samplerSeq.empty())
		kv.emplace_back("sampler-seq", s.samplerSeq);
	if (!s.drySequenceBreaker.empty())
		kv.emplace_back("dry-sequence-breaker", s.drySequenceBreaker);
	if (s.backendSampling)
		kv.emplace_back("backend-sampling", "true");
	return kv;
}

/// Sanitise a preset name for use as an INI section header
std::string sanitiseSectionName(const std::string &name)
{
	std::string out;
	out.reserve(name.size());
	for (char c : name) {
		if (c != '[' && c != ']')
			out += c;
	}
	return trim(out);
}

/// Read file lines (preserving comments and blank lines)
std::vector<std::string> readFileLines(const std::string &path)
{
	std::vector<std::string> lines;
	std::ifstream file(path);
	std::string line;
	while (std::getline(file, line))
		lines.push_back(line);
	return lines;
}

/// Atomically write lines to file via temp + rename
bool writeFileAtomic(const std::string &path,
					 const std::vector<std::string> &lines)
{
	std::string tmpPath = path + ".tmp";
	{
		std::ofstream out(tmpPath);
		if (!out.is_open()) {
			spdlog::error("Failed to open temp file for writing: {}", tmpPath);
			return false;
		}
		for (const auto &line : lines)
			out << line << "\n";
	}

	std::error_code ec;
	std::filesystem::rename(tmpPath, path, ec);
	if (ec) {
		spdlog::error("Failed to rename temp file: {}", ec.message());
		std::filesystem::remove(tmpPath, ec);
		return false;
	}
	return true;
}

/// Find the line range [start, end) for a section in the file lines.
/// Returns {-1, -1} if not found. 'start' is the [section] header line.
std::pair<int, int> findSectionRange(const std::vector<std::string> &lines,
									 const std::string &sectionName)
{
	int start = -1;
	for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
		std::string trimmed = trim(lines[i]);
		if (trimmed.size() >= 2 && trimmed.front() == '[' &&
			trimmed.back() == ']') {
			std::string name = trim(trimmed.substr(1, trimmed.size() - 2));
			if (name == sectionName) {
				start = i;
			} else if (start >= 0) {
				return { start, i };
			}
		}
	}
	if (start >= 0)
		return { start, static_cast<int>(lines.size()) };
	return { -1, -1 };
}

} // namespace

// =========================================================================
// Preset read methods
// =========================================================================

std::optional<Config::ModelPreset>
ModelsIni::getPreset(const std::string &sectionName) const
{
	auto it = m_sections.find(sectionName);
	if (it == m_sections.end() || sectionName == "*")
		return std::nullopt;

	const auto &kv = it->second;
	Config::ModelPreset preset;
	preset.name = sectionName;

	auto modelIt = kv.find("model");
	if (modelIt != kv.end())
		preset.model = modelIt->second;

	// Known inference keys (kebab-case, no prefix)
	static const std::set<std::string> inferenceKeys = {
		"temp",
		"temperature",
		"top-p",
		"top_p",
		"top-k",
		"top_k",
		"min-p",
		"min_p",
		"repeat-penalty",
		"repeat_penalty",
		"repeat-last-n",
		"presence-penalty",
		"presence_penalty",
		"frequency-penalty",
		"frequency_penalty",
		"n-predict",
		"n_predict",
		"seed",
		"samplers",
		// Phase 4 inference params (section D)
		"ignore-eos",
		"logit-bias",
		"adaptive-target",
		"adaptive-decay",
		"grammar-file",
		"json-schema-file",
		"sampler-seq",
		"dry-sequence-breaker",
		"backend-sampling",
	};

	// Split keys into load keys and inference keys
	std::map<std::string, std::string> loadKv;
	std::map<std::string, std::string> infKv;
	for (const auto &[k, v] : kv) {
		if (k == "model")
			continue;
		if (k.rfind("inf.", 0) == 0) {
			infKv[k.substr(4)] = v; // strip "inf." prefix
		} else if (inferenceKeys.count(k)) {
			infKv[k] = v; // non-prefixed inference key
		} else {
			loadKv[k] = v;
		}
	}

	preset.load = iniToLoadSettings(loadKv);
	preset.load.modelPath = preset.model; // sync model path
	preset.inference = iniToInferenceSettings(infKv);

	return preset;
}

std::vector<Config::ModelPreset>
ModelsIni::getPresetsForModel(const std::string &modelPath) const
{
	std::vector<Config::ModelPreset> result;
	for (const auto &[name, kv] : m_sections) {
		if (name == "*")
			continue;
		auto modelIt = kv.find("model");
		if (modelIt != kv.end() && modelIt->second == modelPath) {
			auto preset = getPreset(name);
			if (preset)
				result.push_back(*preset);
		}
	}
	return result;
}

std::vector<ModelsIni::ModelEntry> ModelsIni::getUniqueModelEntries() const
{
	std::vector<ModelEntry> result;
	std::set<std::string> seenPaths;
	for (const auto &[name, kv] : m_sections) {
		if (name == "*")
			continue;
		auto modelIt = kv.find("model");
		if (modelIt == kv.end() || modelIt->second.empty())
			continue;
		if (seenPaths.insert(modelIt->second).second) {
			result.push_back({ name, modelIt->second });
		}
	}
	return result;
}

// =========================================================================
// Preset write methods
// =========================================================================

bool ModelsIni::savePreset(const Config::ModelPreset &preset)
{
	std::string name = sanitiseSectionName(preset.name);
	if (name.empty()) {
		spdlog::error("Cannot save preset with empty name");
		return false;
	}

	// Build ordered section lines
	auto loadKv = loadSettingsToIni(preset.load);
	auto infKv = inferenceSettingsToIni(preset.inference);

	// Read current file
	auto lines = readFileLines(m_filePath);

	// Find existing section
	auto [start, end] = findSectionRange(lines, name);

	// Build replacement lines with comments for readability
	std::vector<std::string> sectionLines;
	sectionLines.push_back("[" + name + "]");
	sectionLines.push_back("model = " + preset.model);
	sectionLines.push_back("; Load settings");
	for (const auto &[k, v] : loadKv)
		sectionLines.push_back(k + " = " + v);
	sectionLines.push_back("; Inference settings");
	for (const auto &[k, v] : infKv)
		sectionLines.push_back(k + " = " + v);
	sectionLines.push_back(""); // blank line after section

	if (start >= 0) {
		// Replace existing section
		lines.erase(lines.begin() + start, lines.begin() + end);
		lines.insert(lines.begin() + start,
					 sectionLines.begin(),
					 sectionLines.end());
	} else {
		// Append new section at end
		if (!lines.empty() && !lines.back().empty())
			lines.push_back("");
		lines.insert(lines.end(), sectionLines.begin(), sectionLines.end());
	}

	if (!writeFileAtomic(m_filePath, lines)) {
		spdlog::error("Failed to write models.ini");
		return false;
	}

	// Reload in-memory state
	load();
	spdlog::info("Saved preset '{}' to models.ini", name);
	return true;
}

bool ModelsIni::renamePreset(const std::string &oldName,
							 const std::string &newName)
{
	std::string sanitised = sanitiseSectionName(newName);
	if (sanitised.empty() || sanitised == "*") {
		spdlog::error("Invalid preset name: '{}'", newName);
		return false;
	}

	// Check new name doesn't already exist
	if (m_sections.find(sanitised) != m_sections.end() && sanitised != oldName) {
		spdlog::warn("Preset name '{}' already exists", sanitised);
		return false;
	}

	auto lines = readFileLines(m_filePath);
	auto [start, end] = findSectionRange(lines, oldName);
	if (start < 0) {
		spdlog::error("Preset '{}' not found for rename", oldName);
		return false;
	}

	// Replace the section header line
	lines[start] = "[" + sanitised + "]";

	if (!writeFileAtomic(m_filePath, lines)) {
		spdlog::error("Failed to write models.ini during rename");
		return false;
	}

	load();
	spdlog::info("Renamed preset '{}' -> '{}'", oldName, sanitised);
	return true;
}

bool ModelsIni::deletePreset(const std::string &sectionName)
{
	auto lines = readFileLines(m_filePath);
	auto [start, end] = findSectionRange(lines, sectionName);
	if (start < 0) {
		spdlog::warn("Preset '{}' not found for deletion", sectionName);
		return false;
	}

	lines.erase(lines.begin() + start, lines.begin() + end);

	if (!writeFileAtomic(m_filePath, lines)) {
		spdlog::error("Failed to write models.ini during delete");
		return false;
	}

	load();
	spdlog::info("Deleted preset '{}'", sectionName);
	return true;
}