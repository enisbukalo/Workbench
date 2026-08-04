#pragma once
#include <algorithm>
#include <string>
#include <vector>

namespace Config {

/**
 * @brief Model loading settings mapped to llama-server CLI parameters.
 *
 * This structure contains all configuration options for loading and
 * initializing the language model. Settings cover model source,
 * GPU acceleration, memory management, context configuration,
 * and various performance optimizations.
 *
 * The settings are organized into logical categories:
 * - **Model source**: Path, URL, or HuggingFace repository
 * - **GPU configuration**: Layer offloading, tensor splitting
 * - **Context and batching**: Context size, batch sizes, parallelism
 * - **KV cache**: Cache types and offloading
 * - **Memory**: Flash attention, memory locking/mapping
 * - **Threading**: CPU thread counts
 * - **Adapters**: LoRA and multimodal projectors
 * - **Speculative decoding**: Draft model configuration
 * - **Chat behavior**: Templates and reasoning format
 *
 * @note Only one model source should be set (modelPath, modelUrl,
 *       or hfRepo/hfFile).
 * @see https://github.com/ggerganov/llama.cpp for detailed documentation
 *
 * @code
 * // Load model from local file
 * LoadSettings settings;
 * settings.modelPath = "models/mistral-7b.gguf";
 * settings.ngpuLayers = "all";  // Offload all layers to GPU
 * settings.ctxSize = 4096;       // 4K context
 * @endcode
 */
struct LoadSettings
{
	/**
	 * @name Model Source
	 *
	 * Specify where to load the model from. Only one source method
	 * should be used at a time.
	 */

	/**
	 * @brief Path to local model file.
	 *
	 * Path to a GGUF model file on the local filesystem.
	 * This is the most common method for loading models.
	 *
	 * Corresponds to: `-m FNAME` or `--model FNAME`
	 * @note Use absolute paths or paths relative to the executable.
	 */
	std::string modelPath;

	/**
	 * @brief URL to download model from.
	 *
	 * Direct URL to download a model file. The model will be
	 * downloaded and cached on first use.
	 *
	 * Corresponds to: `-mu URL` or `--model-url URL`
	 */
	std::string modelUrl;

	/**
	 * @brief HuggingFace repository identifier.
	 *
	 * Format: `username/repository[:quantization]`
	 * Examples:
	 * - `mistralai/Mistral-7B-Instruct-v0.1`
	 * - `TheBloke/Mistral-7B-Instruct-v0.1-GGUF[q4_k_m]`
	 *
	 * Corresponds to: `-hf REPO` or `--hf-repo REPO`
	 */
	std::string hfRepo;

	/**
	 * @brief Specific file in HuggingFace repository.
	 *
	 * When using hfRepo, this specifies the exact filename
	 * to download. If empty, llama.cpp will auto-select.
	 *
	 * Corresponds to: `-hff FILE` or `--hf-file FILE`
	 */
	std::string hfFile;

	/**
	 * @brief HuggingFace access token.
	 *
	 * Required for accessing gated/private repositories.
	 * Obtain from https://huggingface.co/settings/tokens
	 *
	 * Corresponds to: `-hft TOKEN` or `--hf-token TOKEN`
	 * @note Never commit tokens to version control.
	 */
	std::string hfToken;

	/**
	 * @name GPU Configuration
	 *
	 * Control how the model is distributed across GPU devices
	 * for accelerated inference.
	 */

	/**
	 * @brief Number of layers to offload to GPU.
	 *
	 * Determines how many transformer layers run on GPU:
	 * - `-1`: Auto / offload all layers (sentinel for "auto"/"all")
	 * - `0`: CPU only, no GPU offload
	 * - `1`–`99`: Offload exactly N layers
	 *
	 * Corresponds to: `-ngl COUNT`
	 * @default -1 (auto-offload all)
	 * @note Higher values use more VRAM but provide faster inference.
	 * @note When -1, the `-ngl` flag is omitted and llama-server auto-detects.
	 */
	int ngpuLayers = -1;

	/**
	 * @brief Tensor splitting mode across GPUs.
	 *
	 * How to split the model across multiple GPUs:
	 * - "none": Single GPU or CPU only
	 * - "layer": Split by layers (each GPU gets complete layers)
	 * - "row": Split by rows (layers distributed across GPUs)
	 *
	 * Corresponds to: `-sm MODE`
	 * @note Requires multiple GPUs to be effective.
	 */
	std::string splitMode;

	/**
	 * @brief Tensor split ratios for multiple GPUs.
	 *
	 * Comma-separated non-negative decimal ratios, one per GPU.
	 * Values need not sum to any particular total — llama-server divides each
	 * by the total internally. Rounded to nearest hundredth.
	 * Example: "1.50,60.30".
	 *
	 * Validated by `LoadSettings::validate()` (config layer) and
	 * `ModelsPanel::roundToHundredth()` (UI layer) — both must apply
	 * identical rounding rules.
	 *
	 * Corresponds to: `-ts RATIO1,RATIO2,...`
	 */
	std::string tensorSplit;

	/**
	 * @brief Device priority order for loading.
	 *
	 * Comma-separated list of device IDs to prioritize when loading.
	 * Example: "0,1" fills GPU 0 first, then GPU 1.
	 * Use empty for default (auto).
	 *
	 * Corresponds to: `-mg INDEX` (single) or combined with split-mode
	 * @default "" (auto)
	 */
	std::string devicePriority;

	/**
	 * @name Context and Batching
	 *
	 * Control the context window size and request batching behavior.
	 */

	/**
	 * @brief Context size (number of tokens).
	 *
	 * Maximum context window for the model. Common values:
	 * - 2048, 4096, 8192, 16384, 32768
	 *
	 * Corresponds to: `-c SIZE`
	 * @default 0 (use model's default from metadata)
	 * @note Any non-negative value is accepted as-is — it is not rounded to a
	 *       power of 2 and has no enforced upper bound (the model's true maximum
	 *       is unknown here, so the user's value is passed through verbatim).
	 *       A negative value is treated as 0 (model default) by validate().
	 * @note Larger contexts require more memory.
	 */
	int ctxSize = 0;

	/**
	 * @brief Logical batch size.
	 *
	 * Maximum number of tokens to process in a single request.
	 * Affects memory usage and throughput.
	 *
	 * Corresponds to: `-b SIZE`
	 * @default 2048
	 */
	int batchSize = 2048;

	/**
	 * @brief Physical batch size.
	 *
	 * Internal batch size for computation. Should typically be
	 * a divisor of batchSize.
	 *
	 * Corresponds to: `-ub SIZE`
	 * @default 512
	 */
	int ubatchSize = 512;

	/**
	 * @brief Number of parallel server slots.
	 *
	 * How many concurrent requests the server can handle.
	 * Higher values increase memory usage.
	 *
	 * Corresponds to: `-np COUNT`
	 * @default 4
	 */
	int parallel = 4;

	/**
	 * @name KV Cache Configuration
	 *
	 * Key-Value cache settings for attention computation.
	 */

	/**
	 * @brief Data type for K-cache.
	 *
	 * Precision for key cache storage:
	 * - "f16": 16-bit float (default, balanced)
	 * - "q8_0": 8-bit quantized (saves memory)
	 * - "q4_0": 4-bit quantized (maximum compression)
	 *
	 * Corresponds to: `-ctk TYPE`
	 * @default "f16"
	 */
	std::string cacheTypeK = "f16";

	/**
	 * @brief Data type for V-cache.
	 *
	 * Precision for value cache storage. Options same as cacheTypeK.
	 *
	 * Corresponds to: `-ctv TYPE`
	 * @default "f16"
	 */
	std::string cacheTypeV = "f16";

	/**
	 * @brief Enable KV cache offloading to GPU.
	 *
	 * When enabled, KV cache is stored in GPU memory for faster
	 * access. Requires sufficient VRAM.
	 *
	 * Corresponds to: `--kv-offload`
	 * @default true
	 */
	bool kvOffload = true;

	/**
	 * @brief Use unified KV cache buffer.
	 *
	 * Use single unified KV buffer shared across all sequences.
	 * Improves memory efficiency when using multiple slots.
	 *
	 * Corresponds to: `--kv-unified`
	 * @default true when parallel is auto, false otherwise
	 */
	bool kvUnified = true;

	/**
	 * @name Memory Management
	 *
	 * Advanced memory optimization settings.
	 */

	/**
	 * @brief Flash Attention mode.
	 *
	 * Memory-efficient attention implementation:
	 * - "auto": Automatic selection (default)
	 * - "on": Force enable
	 * - "off": Force disable
	 *
	 * Corresponds to: `-fa MODE`
	 * @default "on"
	 * @note Can significantly reduce memory usage for large contexts.
	 */
	std::string flashAttn = "on";

	/**
	 * @brief Lock model in RAM.
	 *
	 * Prevents the model from being swapped to disk. Useful for
	 * ensuring consistent performance but uses more RAM.
	 *
	 * Corresponds to: `--mlock`
	 * @default false
	 */
	bool mlock = false;

	/**
	 * @brief Use memory-mapped files.
	 *
	 * Loads model via mmap for potentially faster startup and
	 * lower peak memory usage. May cause swapping on large models.
	 *
	 * Corresponds to: `--mmap` (enable) or `--no-mmap` (disable)
	 * @default false
	 */
	bool mmap = false;

	/**
	 * @name Threading
	 *
	 * CPU thread pool configuration.
	 */

	/**
	 * @brief Number of threads for general computation.
	 *
	 * Thread count for non-batched operations. Use -1 for
	 * automatic detection.
	 *
	 * Corresponds to: `-t COUNT`
	 * @default -1 (auto)
	 */
	int threads = -1;

	/**
	 * @brief Number of threads for batched computation.
	 *
	 * Thread count for batched operations. Use -1 for
	 * automatic detection.
	 *
	 * Corresponds to: `-tb COUNT`
	 * @default -1 (auto)
	 */
	int threadsBatch = -1;

	/**
	 * @name Adapters and Multimodal
	 *
	 * Additional model components and adapters.
	 */

	/**
	 * @brief Path to LoRA adapter file.
	 *
	 * Apply a LoRA fine-tune on top of the base model.
	 *
	 * Corresponds to: `--lora FNAME`
	 * @note Multiple LoRAs can be chained with comma-separated paths.
	 */
	std::string lora;

	/**
	 * @brief Path to multimodal projector file.
	 *
	 * Projector file for vision-language models (e.g., LLaVA).
	 * Required for image input support.
	 *
	 * Corresponds to: `-mm FILE` or `--mmproj FILE`
	 */
	std::string mmproj;

	/**
	 * @name Speculative Decoding
	 *
	 * Settings for speculative/optimistic decoding with draft models.
	 */

	/**
	 * @brief Path to draft model for speculative decoding.
	 *
	 * A smaller model that generates candidate tokens for the
	 * main model to verify. Can significantly speed up generation.
	 *
	 * Corresponds to: `-md FNAME` or `--model-draft FNAME`
	 * @note Draft model should be smaller and faster than main model.
	 */
	std::string modelDraft;

	/**
	 * @brief Maximum speculative tokens.
	 *
	 * How many tokens the draft model can propose. Use -1 for
	 * automatic selection.
	 *
	 * Corresponds to: `--spec-draft-n-max N`
	 * @default -1 (auto)
	 */
	int draftMax = -1;

	/**
	 * @brief Speculative decoding type(s).
	 *
	 * Selects which speculative decoding strategy llama-server uses.
	 * Valid values: none, draft-simple, draft-eagle3, draft-mtp, draft-dflash,
	 *   ngram-simple, ngram-map-k, ngram-map-k4v, ngram-mod, ngram-cache
	 *
	 * Corresponds to: `--spec-type TYPE[,TYPE,...]`
	 * @default "" (flag omitted; server defaults to none)
	 */
	std::string specType;

	/**
	 * @brief KV cache K data type for the draft model.
	 * Corresponds to: `-ctkd` / `--cache-type-k-draft`
	 * Allowed: f32,f16,bf16,q8_0,q4_0,q4_1,iq4_nl,q5_0,q5_1. @default "f16"
	 */
	std::string cacheTypeKDraft = "f16";

	/**
	 * @brief KV cache V data type for the draft model.
	 * Corresponds to: `-ctvd` / `--cache-type-v-draft`. @default "f16"
	 */
	std::string cacheTypeVDraft = "f16";

	/**
	 * @brief Comma-separated device list for offloading the draft model.
	 * Corresponds to: `-devd` / `--device-draft`. Example: "CUDA0,CUDA1".
	 * Device names come from `--list-devices`, not bare indices. @default ""
	 * (auto)
	 */
	std::string deviceDraft;

	/**
	 * @name Chat Behavior
	 *
	 * Settings for chat template and reasoning output formatting.
	 */

	/**
	 * @brief Chat template name.
	 *
	 * Built-in chat template to use for message formatting.
	 * Examples: "chatml", "llama-2-chat", "alpaca"
	 *
	 * Corresponds to: `--chat-template NAME`
	 * @note If empty, model's default template is used.
	 */
	std::string chatTemplate;

	/**
	 * @brief Reasoning format for models with reasoning capability.
	 *
	 * How to handle reasoning content in models like OpenAI o1:
	 * - "default": Standard handling
	 * - "hidden": Hide reasoning from output
	 * - "separate": Output reasoning separately
	 *
	 * Corresponds to: `--reasoning-format FORMAT`
	 */
	std::string reasoningFormat;

	/**
	 * @brief Preserve reasoning content across multi-turn chat (Qwen3-family).
	 * When true, serializes as `--chat-template-kwargs '{"preserve_thinking":
	 * true}'`, keeping `reasoning_content` on prior assistant messages. @default
	 * false
	 */
	bool preserveThinking = false;

	/**
	 * @name Miscellaneous
	 */

	/**
	 * @brief Auto-fit model to available device memory.
	 *
	 * Automatically adjusts layer offloading to fit the model
	 * within available GPU/CPU memory.
	 *
	 * Corresponds to: `--fit`
	 * @default true
	 * @note Recommended for most users; disables manual ngpuLayers.
	 */
	bool fit = true;

	/**
	 * @name Issue #103 section C — additional load params
	 *
	 * Per-model load settings written to models.ini. Never emitted by
	 * buildCommandArgs (router mode reads them from models.ini per #20851).
	 */

	/**
	 * @brief Number of MoE expert layers to keep on CPU.
	 *
	 * Corresponds to: `--n-cpu-moe N` (`-ncmoe`)
	 * @default -1 (omitted)
	 */
	int nCpuMoe = -1;

	/**
	 * @brief Keep all MoE expert weights on the CPU.
	 *
	 * Corresponds to: `--cpu-moe`
	 * @default false
	 */
	bool cpuMoe = false;

	/**
	 * @brief Per-tensor device override spec.
	 *
	 * Free-form `regex=device` specification.
	 *
	 * Corresponds to: `--override-tensor SPEC` (`-ot`)
	 */
	std::string overrideTensor;

	/**
	 * @brief RoPE frequency scaling method.
	 *
	 * One of {none, linear, yarn}. Invalid values reset to "" by validate().
	 *
	 * Corresponds to: `--rope-scaling {none,linear,yarn}`
	 * @default "" (omitted)
	 */
	std::string ropeScaling;

	/**
	 * @brief RoPE context scaling factor.
	 *
	 * Corresponds to: `--rope-scale N`
	 * @default 0.0 (omitted)
	 */
	double ropeScale = 0.0;

	/**
	 * @brief RoPE base frequency.
	 *
	 * Corresponds to: `--rope-freq-base N`
	 * @default 0.0 (omitted)
	 */
	double ropeFreqBase = 0.0;

	/**
	 * @brief RoPE frequency scaling factor.
	 *
	 * Corresponds to: `--rope-freq-scale N`
	 * @default 0.0 (omitted)
	 */
	double ropeFreqScale = 0.0;

	/**
	 * @brief Original context size used for YaRN extension.
	 *
	 * Corresponds to: `--yarn-orig-ctx N`
	 * @default 0 (omitted)
	 */
	int yarnOrigCtx = 0;

	/**
	 * @brief YaRN extrapolation mix factor.
	 *
	 * Corresponds to: `--yarn-ext-factor N`
	 * @default -1.0 (omitted)
	 */
	double yarnExtFactor = -1.0;

	/**
	 * @brief YaRN attention scaling factor.
	 *
	 * Corresponds to: `--yarn-attn-factor N`
	 * @default 1.0 (omitted at default)
	 */
	double yarnAttnFactor = 1.0;

	/**
	 * @brief YaRN low-correction-dim factor.
	 *
	 * Corresponds to: `--yarn-beta-slow N`
	 * @default 1.0 (omitted at default)
	 */
	double yarnBetaSlow = 1.0;

	/**
	 * @brief YaRN high-correction-dim factor.
	 *
	 * Corresponds to: `--yarn-beta-fast N`
	 * @default 32.0 (omitted at default)
	 */
	double yarnBetaFast = 32.0;

	/**
	 * @brief Use full-size SWA cache instead of the sliding window.
	 *
	 * Corresponds to: `--swa-full`
	 * @default false
	 */
	bool swaFull = false;

	/**
	 * @brief Number of tokens to keep from the initial prompt on context shift.
	 *
	 * Corresponds to: `--keep N`
	 * @default -1 (omitted)
	 */
	int keep = -1;

	/**
	 * @brief NUMA memory allocation strategy.
	 *
	 * One of {distribute, isolate, numactl}. Invalid values reset to "" by
	 * validate().
	 *
	 * Corresponds to: `--numa TYPE`
	 * @default "" (omitted)
	 */
	std::string numa;

	/**
	 * @brief Auto-fit target backend.
	 *
	 * One of {auto, vram, ram}. Invalid values reset to "" by validate().
	 * Coexists with the `fit` bool (which toggles fitting on/off).
	 *
	 * Corresponds to: `--fit-target TARGET`
	 * @default "" (omitted)
	 */
	std::string fitTarget;

	/**
	 * @brief Context size to target when auto-fitting.
	 *
	 * Corresponds to: `--fit-ctx N`
	 * @default 0 (omitted)
	 */
	int fitCtx = 0;

	/**
	 * @brief Validate tensor data on load.
	 *
	 * Corresponds to: `--check-tensors`
	 * @default false
	 */
	bool checkTensors = false;

	/**
	 * @brief Override a model metadata key.
	 *
	 * Free-form `KEY=TYPE:VALUE`.
	 *
	 * Corresponds to: `--override-kv KEY=TYPE:VALUE`
	 */
	std::string overrideKv;

	/**
	 * @brief Scaled LoRA adapter.
	 *
	 * `FNAME=SCALE`.
	 *
	 * Corresponds to: `--lora-scaled FNAME SCALE`
	 */
	std::string loraScaled;

	/**
	 * @brief Control vector file.
	 *
	 * Corresponds to: `--control-vector FNAME`
	 */
	std::string controlVector;

	/**
	 * @brief Scaled control vector.
	 *
	 * `FNAME=SCALE`.
	 *
	 * Corresponds to: `--control-vector-scaled FNAME SCALE`
	 */
	std::string controlVectorScaled;

	/**
	 * @brief Minimum draft tokens for speculative decoding.
	 *
	 * Corresponds to: `--spec-draft-n-min N`
	 * @default -1 (omitted)
	 */
	int specDraftNMin = -1;

	/**
	 * @brief Minimum draft probability for speculative decoding.
	 *
	 * Corresponds to: `--spec-draft-p-min N`
	 * @default -1.0 (omitted)
	 */
	double specDraftPMin = -1.0;

	/**
	 * @brief Draft split probability for speculative decoding.
	 *
	 * Corresponds to: `--spec-draft-p-split N`
	 * @default -1.0 (omitted)
	 */
	double specDraftPSplit = -1.0;

	/**
	 * @brief Keep draft-model MoE expert weights on the CPU.
	 *
	 * Corresponds to: `--cpu-moe-draft`
	 * @default false
	 */
	bool cpuMoeDraft = false;

	/**
	 * @brief Clamp all numeric fields to valid ranges in-place.
	 *
	 * Called automatically at the end of from_json. Safe to call
	 * explicitly (e.g., after programmatic mutation). No-op when
	 * all values are already valid.
	 */
	void validate() noexcept;
};

/**
 * @brief Inference/sampling settings mapped to llama-server CLI parameters.
 *
 * This structure contains all configuration options for text generation
 * and sampling. Settings control randomness, repetition penalties,
 * grammar constraints, and various advanced sampling algorithms.
 *
 * The settings are organized into logical categories:
 * - **Generation limit**: Maximum tokens to generate
 * - **Sampling chain**: Order of sampling algorithms applied
 * - **Core sampling**: Temperature, top-k, top-p, etc.
 * - **XTC sampling**: Exclusive sampling for diversity
 * - **Penalties**: Repetition and presence/frequency penalties
 * - **DRY sampling**: Depth-based repetition penalty
 * - **Dynamic temperature**: Adaptive temperature adjustment
 * - **Mirostat**: Entropy-based sampling
 * - **Constrained generation**: Grammar and JSON constraints
 *
 * @note For creative writing: higher temperature (0.8-1.2), moderate topP (0.9)
 * @note For factual/analytical: lower temperature (0.2-0.5), higher topP
 * (0.95+)
 * @see https://github.com/ggerganov/llama.cpp for detailed algorithm
 * documentation
 *
 * @code
 * // Creative writing settings
 * InferenceSettings creative;
 * creative.temperature = 1.2f;
 * creative.topP = 0.9f;
 * creative.repeatPenalty = 1.1f;
 *
 * // Precise/analytical settings
 * InferenceSettings analytical;
 * analytical.temperature = 0.3f;
 * analytical.topP = 0.95f;
 * analytical.topK = 20;
 * @endcode
 */
struct InferenceSettings
{
	/**
	 * @name Generation Limits
	 *
	 * Control how much text is generated.
	 */

	/**
	 * @brief Maximum number of tokens to generate.
	 *
	 * Limits the response length. Use -1 for unlimited generation
	 * (will stop on end-of-sequence token).
	 *
	 * Corresponds to: `-n COUNT` or `--n-predict COUNT`
	 * @default -1 (unlimited)
	 * @note Very high values may cause memory issues with long contexts.
	 */
	int nPredict = -1;

	/**
	 * @name Sampling Chain
	 *
	 * Configure the order in which sampling algorithms are applied.
	 */

	/**
	 * @brief Semicolon-separated list of samplers in order.
	 *
	 * Defines the pipeline of sampling algorithms. Each sampler
	 * further filters the token probability distribution.
	 *
	 * Available samplers:
	 * - `penalties`: Apply repetition penalties
	 * - `dry`: DRY (Depth-based Repetition) penalty
	 * - `top_n_sigma`: Top N-sigma sampling
	 * - `topK`: Top-K sampling
	 * - `topP`: Nucleus (top-p) sampling
	 * - `minP`: Minimum probability sampling
	 * - `xtc`: XTC (exclusive) sampling
	 * - `temperature`: Temperature scaling
	 *
	 * Corresponds to: `--samplers LIST`
	 * @default
	 * "penalties;dry;top_n_sigma;topK;typ_p;topP;minP;xtc;temperature"
	 * @note Order matters - samplers are applied left to right.
	 */
	std::string samplers;

	/**
	 * @name Core Sampling Parameters
	 *
	 * Fundamental randomness and selection controls.
	 */

	/**
	 * @brief Random seed for reproducible generation.
	 *
	 * Use a fixed seed for reproducible results. Use -1 for
	 * a random seed on each generation.
	 *
	 * Corresponds to: `-s SEED` or `--seed SEED`
	 * @default -1 (random)
	 * @note Same seed + same prompt = same output (deterministic).
	 */
	int seed = -1;

	/**
	 * @brief Temperature for probability distribution scaling.
	 *
	 * Controls randomness in token selection:
	 * - Lower (0.1-0.5): More deterministic, focused outputs
	 * - Medium (0.6-0.9): Balanced creativity and coherence
	 * - Higher (1.0+): More creative, unpredictable outputs
	 *
	 * Corresponds to: `--temp VALUE`
	 * @default 0.8
	 * @range 0.0 to ~2.0 (higher values possible but rarely useful)
	 * @note 0.0 would be completely deterministic (argmax).
	 */
	double temperature = 0.8;

	/**
	 * @brief Top-K sampling limit.
	 *
	 * Restricts sampling to the K most probable tokens.
	 * Lower values = more focused, higher values = more diverse.
	 *
	 * Corresponds to: `--top-k K`
	 * @default 40
	 * @range 0 (disabled) to vocabulary size
	 * @note Often used together with topP for best results.
	 */
	int topK = 40;

	/**
	 * @brief Top-P (nucleus) sampling threshold.
	 *
	 * Samples from the smallest set of tokens whose cumulative
	 * probability exceeds P. Higher values allow more unlikely tokens.
	 *
	 * Corresponds to: `--top-p VALUE`
	 * @default 0.95
	 * @range 0.0 to 1.0
	 * @note 1.0 disables the filter (all tokens considered).
	 */
	double topP = 0.95;

	/**
	 * @brief Minimum probability threshold for tokens.
	 *
	 * Only considers tokens with probability >= minP * max_probability.
	 * Provides an alternative to topK/topP.
	 *
	 * Corresponds to: `--min-p VALUE`
	 * @default 0.05
	 * @range 0.0 (disabled) to 1.0
	 * @note Useful when you want a relative threshold rather than absolute.
	 */
	double minP = 0.05;

	/**
	 * @brief Top N-sigma sampling threshold.
	 *
	 * Keeps tokens within N standard deviations of the mean probability.
	 * Statistical approach to diversity control.
	 *
	 * Corresponds to: `--top-nsigma VALUE`
	 * @default -1.0 (disabled)
	 * @range -1.0 (disabled) to ~4.0
	 * @note Alternative to topK/topP with statistical interpretation.
	 */
	double topNsigma = -1.0;

	/**
	 * @brief Typical sampling parameter.
	 *
	 * Samples based on pointwise mutual information. Values near
	 * 1.0 produce "typical" text; lower values produce more surprising text.
	 *
	 * Corresponds to: `--typical VALUE`
	 * @default 1.0 (disabled)
	 * @range 0.0 to 1.0
	 * @note Based on "locally typical sampling" research.
	 */
	double typicalP = 1.0;

	/**
	 * @name XTC Sampling
	 *
	 * Exclusive sampling for increased diversity.
	 */

	/**
	 * @brief XTC sampling probability.
	 *
	 * Probability of using exclusive sampling on each token.
	 * Higher values = more diverse but potentially incoherent output.
	 *
	 * Corresponds to: `--xtc-probability VALUE`
	 * @default 0.0 (disabled)
	 * @range 0.0 to 1.0
	 */
	double xtcProbability = 0.0;

	/**
	 * @brief XTC sampling threshold.
	 *
	 * Threshold for XTC's exclusive sampling decision.
	 *
	 * Corresponds to: `--xtc-threshold VALUE`
	 * @default 0.1
	 * @range 0.0 to 1.0
	 */
	double xtcThreshold = 0.1;

	/**
	 * @name Repetition Penalties
	 *
	 * Controls to reduce repetitive output.
	 */

	/**
	 * @brief Number of tokens to check for repetition.
	 *
	 * Looks back this many tokens when applying repeatPenalty.
	 * Use -1 to check the entire context.
	 *
	 * Corresponds to: `--repeat-last-n N`
	 * @default 64
	 * @range 0 (disabled) to context size, or -1 for full context
	 */
	int repeatLastN = 64;

	/**
	 * @brief Repetition penalty factor.
	 *
	 * Penalizes tokens that have appeared recently. Higher values
	 * strongly discourage repetition; lower values allow it.
	 *
	 * Corresponds to: `--repeat-penalty VALUE`
	 * @default 1.0 (disabled)
	 * @range 1.0 to ~2.0
	 * @note >1.0 penalizes repetition; <1.0 encourages it.
	 */
	double repeatPenalty = 1.0;

	/**
	 * @brief Presence penalty.
	 *
	 * Penalizes tokens that have appeared at all in the context,
	 * regardless of frequency. Encourages topic diversity.
	 *
	 * Corresponds to: `--presence-penalty VALUE`
	 * @default 0.0 (disabled)
	 * @range -2.0 to 2.0
	 * @note Positive values discourage new topics; negative encourage them.
	 */
	double presencePenalty = 0.0;

	/**
	 * @brief Frequency penalty.
	 *
	 * Penalizes tokens proportional to how often they've appeared.
	 * Discourages repetitive phrasing.
	 *
	 * Corresponds to: `--frequency-penalty VALUE`
	 * @default 0.0 (disabled)
	 * @range -2.0 to 2.0
	 * @note Similar to OpenAI's frequencyPenalty parameter.
	 */
	double frequencyPenalty = 0.0;

	/**
	 * @name DRY Sampling
	 *
	 * Depth-based repetition penalty for more nuanced control.
	 */

	/**
	 * @brief DRY penalty multiplier.
	 *
	 * Strength of the DRY penalty. Higher values more strongly
	 * penalize repetitive sequences.
	 *
	 * Corresponds to: `--dry-multiplier VALUE`
	 * @default 0.0 (disabled)
	 * @range 0.0 to ~5.0
	 */
	double dryMultiplier = 0.0;

	/**
	 * @brief DRY penalty base value.
	 *
	 * Base value for DRY penalty calculation.
	 *
	 * Corresponds to: `--dry-base VALUE`
	 * @default 1.75
	 */
	double dryBase = 1.75;

	/**
	 * @brief DRY allowed repetition length.
	 *
	 * Length of sequences that are allowed to repeat without penalty.
	 *
	 * Corresponds to: `--dry-allowed-length N`
	 * @default 2
	 */
	int dryAllowedLength = 2;

	/**
	 * @brief DRY penalty lookback window.
	 *
	 * How far back to look for DRY penalty. Use -1 for full context.
	 *
	 * Corresponds to: `--dry-penalty-last-n N`
	 * @default -1 (full context)
	 */
	int dryPenaltyLastN = -1;

	/**
	 * @name Dynamic Temperature
	 *
	 * Adaptive temperature based on output entropy.
	 */

	/**
	 * @brief Dynamic temperature adjustment range.
	 *
	 * Maximum adjustment to temperature based on entropy.
	 * Higher values allow more variation.
	 *
	 * Corresponds to: `--dynatemp-range VALUE`
	 * @default 0.0 (disabled)
	 * @range 0.0 to ~2.0
	 */
	double dynatempRange = 0.0;

	/**
	 * @brief Dynamic temperature exponent.
	 *
	 * Exponent for the dynamic temperature adjustment formula.
	 *
	 * Corresponds to: `--dynatemp-exp VALUE`
	 * @default 1.0
	 */
	double dynatempExp = 1.0;

	/**
	 * @name Mirostat Sampling
	 *
	 * Entropy-based adaptive sampling algorithm.
	 */

	/**
	 * @brief Mirostat version.
	 *
	 * Which version of Mirostat to use:
	 * - 0: Disabled
	 * - 1: Mirostat v1
	 * - 2: Mirostat v2 (improved)
	 *
	 * Corresponds to: `--mirostat VERSION`
	 * @default 0 (disabled)
	 * @note Mirostat adjusts temperature dynamically to maintain
	 *       target information content.
	 */
	int mirostat = 0;

	/**
	 * @brief Mirostat learning rate (eta).
	 *
	 * How quickly Mirostat adapts the temperature.
	 * Lower = slower, more stable; Higher = faster adaptation.
	 *
	 * Corresponds to: `--mirostat-lr VALUE`
	 * @default 0.1
	 * @range 0.0 to ~1.0
	 */
	double mirostatLr = 0.1;

	/**
	 * @brief Mirostat target entropy (tau).
	 *
	 * Target information content in bits. Higher = more surprising
	 * and diverse output; Lower = more predictable output.
	 *
	 * Corresponds to: `--mirostat-ent VALUE`
	 * @default 5.0
	 * @range ~1.0 to ~10.0
	 */
	double mirostatEnt = 5.0;

	/**
	 * @name Constrained Generation
	 *
	 * Force output to match a specific format.
	 */

	/**
	 * @brief BNF-style grammar for constrained generation.
	 *
	 * Grammar specification that constrains output to valid
	 * sequences. Useful for generating code, JSON, or other
	 * structured formats.
	 *
	 * Corresponds to: `--grammar GRAMMAR`
	 * @note Grammar syntax follows llama.cpp's grammar format.
	 * @see json_schema for easier JSON constraint specification.
	 */
	std::string grammar;

	/**
	 * @brief JSON schema for structured output.
	 *
	 * JSON schema that the output must conform to. Easier than
	 * writing a grammar manually.
	 *
	 * Corresponds to: `-j SCHEMA` or `--json-schema SCHEMA`
	 * @note Automatically generates appropriate grammar from schema.
	 * @code
	 * // Example JSON schema for a person object
	 * R"""({
	 *   "type": "object",
	 *   "properties": {
	 *     "name": {"type": "string"},
	 *     "age": {"type": "integer"}
	 *   }
	 * })""";
	 * @endcode
	 */
	std::string jsonSchema;

	/**
	 * @brief Path to JSON schema file for structured output.
	 *
	 * External file containing a JSON schema that constrains the model's
	 * output format. Coexists with `jsonSchema` (inline schema string).
	 *
	 * Corresponds to: `--json-schema-file PATH`
	 * @note When both grammarFile and jsonSchemaFile are set, grammar takes
	 * precedence.
	 */
	std::string jsonSchemaFile;

	/**
	 * @brief Path to BNF grammar file for constrained generation.
	 *
	 * External file containing a grammar specification that constrains
	 * output to valid sequences. Coexists with `grammar` (inline grammar
	 * string).
	 *
	 * Corresponds to: `--grammar-file PATH`
	 * @note When both grammarFile and jsonSchemaFile are set, grammar takes
	 * precedence.
	 */
	std::string grammarFile;

	/**
	 * @brief Character-shorthand sampler sequence chain.
	 *
	 * Short character codes (e.g., "tKpT") that define the order of
	 * samplers applied during generation. Distinct from `samplers` which
	 * uses full sampler names. Provides a compact alternative syntax.
	 *
	 * Corresponds to: `--sampler-seq CHAIN`
	 */
	std::string samplerSeq;

	/**
	 * @brief DRY sequence breaker tokens.
	 *
	 * Additional token sequences that break the DRY (Depth-based Repetition)
	 * penalty lookback window. Can be specified multiple times for different
	 * breakers. Useful for controlling repetition in creative writing.
	 *
	 * Corresponds to: `--dry-sequence-breaker SEQUENCE`
	 */
	std::string drySequenceBreaker;

	/**
	 * @brief Adaptive target context length multiplier.
	 *
	 * When set, the server dynamically adjusts the target context length
	 * based on token throughput. Use 0 to disable (server default).
	 *
	 * Corresponds to: `--adaptive-target VALUE`
	 * @default 0 (disabled)
	 */
	double adaptiveTarget = 0.0;

	/**
	 * @brief Adaptive decay rate for target context adjustment.
	 *
	 * Decay factor applied when adapting the target context length. Works
	 * in conjunction with `adaptiveTarget`. Use 0 to disable (server default).
	 *
	 * Corresponds to: `--adaptive-decay VALUE`
	 * @default 0 (disabled)
	 */
	double adaptiveDecay = 0.0;

	/**
	 * @brief Ignore end-of-sequence token during generation.
	 *
	 * When true, the model continues generating even after producing an
	 * EOS token instead of stopping. Useful for continuous text generation.
	 *
	 * Corresponds to: `--ignore-eos`
	 * @default false
	 */
	bool ignoreEos = false;

	/**
	 * @brief Logit bias modifications for specific tokens.
	 *
	 * Comma-separated pairs of token ID and bias value in the format
	 * "TOKEN_ID(+/-)BIAS". Positive bias increases probability, negative
	 * decreases it. Can be specified multiple times for different tokens.
	 *
	 * Corresponds to: `-l TOKEN_ID(+/−)BIAS` or `--logit-bias ...`
	 */
	std::string logitBias;

	/**
	 * @brief Enable backend sampling mode.
	 *
	 * When true, uses the server's built-in sampling backend instead of
	 * client-side sampling. Changes how generation parameters are applied.
	 *
	 * Corresponds to: `--backend-sampling`
	 * @default false
	 */
	bool backendSampling = false;

	/**
	 * @brief Clamp all numeric fields to valid ranges in-place.
	 *
	 * Called automatically at the end of from_json. No-op when
	 * all values are already valid.
	 */
	void validate() noexcept;
};

/**
 * @brief Model preset structure.
 *
 * A named configuration that bundles a model selection with
 * its associated inference settings. Presets allow users to
 * quickly switch between different model configurations.
 *
 * Presets are stored in the `presets` vector of UserConfig and
 * can be selected from the UI. Each preset represents a complete
 * configuration for a specific use case or model.
 *
 * @code
 * // Create a preset for a creative writing model
 * ModelPreset creative;
 * creative.name = "Creative Writer";
 * creative.model = "models/mistral-7b-instruct.gguf";
 * creative.inference.temperature = 1.2f;
 * creative.inference.top_p = 0.9f;
 *
 * // Add to config
 * config.presets.push_back(creative);
 * @endcode
 *
 * @see UserConfig::presets for the collection of all presets
 */
struct ModelPreset
{
	/**
	 * @brief Human-readable name for the preset.
	 *
	 * Displayed in the UI for selection. Should be descriptive
	 * and unique among presets.
	 *
	 * @note Examples: "Creative Writer", "Code Assistant", "Analysis Mode"
	 */
	std::string name;

	/**
	 * @brief Path or identifier for the model.
	 *
	 * The model file path, URL, or HuggingFace repository
	 * that this preset uses.
	 *
	 * @note This is typically a local path like "models/model.gguf"
	 * @see LoadSettings::model_path for format details
	 */
	std::string model;

	/**
	 * @brief Load settings specific to this preset.
	 *
	 * GPU layers, context size, batch size, and other model loading
	 * parameters optimized for this preset's use case.
	 *
	 * @see LoadSettings for parameter documentation
	 */
	LoadSettings load;

	/**
	 * @brief Inference settings specific to this preset.
	 *
	 * Sampling and generation parameters optimized for this
	 * model's characteristics and intended use case.
	 *
	 * @note These settings override the global inference settings
	 *       when this preset is selected.
	 * @see InferenceSettings for parameter documentation
	 */
	InferenceSettings inference;

	/**
	 * @brief Validate preset name; ensure nested settings are valid.
	 *
	 * Sets name to "Unnamed" if empty. Nested load/inference are
	 * validated via their own from_json chains.
	 */
	void validate();
};

/**
 * @brief Model discovery settings.
 *
 * Configuration for automatic model file discovery. Specifies
 * directories to scan recursively for .gguf model files.
 *
 * @note No default search paths are provided. The user must
 *       explicitly configure at least one path in modelSearchPaths
 *       for model discovery to work. If empty, no models will be
 *       discovered and the model dropdown will remain empty.
 *
 * @code
 * // Configure search paths
 * DiscoverySettings discovery;
 * discovery.modelSearchPaths = {
 *     "/path/to/models",
 *     "~/llama.cpp/models",
 *     "C:\\AI\\models"
 * };
 * @endcode
 */
struct DiscoverySettings
{
	/**
	 * @brief Directory to scan for model files.
	 *
	 * Directory path to recursively search for .gguf files.
	 * Path can be absolute or relative, and may use ~ for home directory.
	 *
	 * @note Empty by default - user must explicitly configure search path.
	 * @note Tilde (~) is expanded to the user's home directory.
	 * @note Non-existent directories are silently skipped during scanning.
	 * @note Scanning is recursive - subdirectories are included.
	 *
	 * @note This path is also used as --models-dir for llama-server router mode.
	 */
	std::string modelSearchPath;

	/**
	 * @brief File filter patterns to exclude from model discovery.
	 *
	 * List of glob-style patterns to exclude files from the model list.
	 * Supports single `*` wildcard matching (case-insensitive):
	 * - `mmproj*` → matches filenames starting with "mmproj"
	 * - `*mmproj` → matches filenames ending with "mmproj"
	 * - `*mmproj*` → matches filenames containing "mmproj"
	 * - No `*` → substring match anywhere in filename
	 *
	 * @note Defaults to `{"mmproj*"}` to exclude multimodal projector files.
	 * @note Patterns are matched against the filename only (not full path).
	 * @note Matching is case-insensitive.
	 * @note Multiple patterns can be specified; file is excluded if ANY match.
	 *
	 * @code
	 * // Exclude mmproj files and draft models
	 * discovery.fileFilter = {"mmproj*", "*draft*"};
	 *
	 * // Show all files (no filtering)
	 * discovery.fileFilter = {};
	 * @endcode
	 */
	std::vector<std::string> fileFilter = { "mmproj*" };

	/**
	 * @brief No-op validate for API consistency.
	 *
	 * DiscoverySettings accepts any value — no clamping needed.
	 */
	void validate() noexcept
	{
	}
};

} // namespace Config
