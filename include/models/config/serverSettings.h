#pragma once
#include <algorithm>
#include <string>

namespace Config {

/**
 * @brief Server settings mapped to llama-server CLI parameters.
 *
 * This structure contains all configuration options for the HTTP server
 * that serves the llama.cpp inference engine. Each field corresponds to
 * one or more command-line flags accepted by llama-server.
 *
 * The settings are organized into logical categories:
 * - **Network**: Host, port, API keys, timeouts, HTTP threads
 * - **SSL**: TLS certificate and key files for HTTPS
 * - **Static serving**: Paths for serving static files and media
 * - **Server behavior**: Web UI, embedding mode, batching, caching
 * - **Endpoints**: Metrics, properties, and slot management
 *
 * @note Most boolean fields default to values that enable features.
 * @note The host "127.0.0.1" binds to localhost only; use "0.0.0.0"
 *       to listen on all interfaces.
 * @see https://github.com/ggerganov/llama.cpp for llama-server documentation
 *
 * @code
 * // Configure server to listen on all interfaces
 * ServerSettings settings;
 * settings.host = "0.0.0.0";
 * settings.port = 8080;
 * settings.apiKey = "secret-key";
 * @endcode
 */
struct ServerSettings
{
	/**
	 * @name Executable
	 */

	/**
	 * @brief Path to the llama-server executable.
	 *
	 * Full path to the llama-server binary on the local filesystem.
	 * On Linux this can also be just the binary name if it's on PATH.
	 *
	 * @note Must be set before launching the server.
	 */
	std::string executablePath;

	/**
	 * @name Network Settings
	 *
	 * These fields control how the server binds to network interfaces
	 * and handles authentication.
	 */

	/**
	 * @brief Host address to bind the server.
	 *
	 * Default "127.0.0.1" binds to localhost only. Use "0.0.0.0" to
	 * listen on all network interfaces, or specify a specific IP.
	 *
	 * Corresponds to: `--host ADDRESS`
	 * @default "127.0.0.1"
	 */
	std::string host = "127.0.0.1";

	/**
	 * @brief Port number to listen on.
	 *
	 * The HTTP server will listen on this port. Common choices:
	 * - 8080: Default, non-privileged port
	 * - 80: HTTP (requires root/admin)
	 * - 443: HTTPS (requires root/admin)
	 *
	 * Corresponds to: `--port PORT`
	 * @default 8080
	 * @range 1-65535
	 */
	int port = 8080;

	/**
	 * @brief API key for request authentication.
	 *
	 * Clients must include this key in the "Authorization: Bearer KEY"
	 * header. If empty, no authentication is required.
	 *
	 * Corresponds to: `--api-key KEY`
	 * @note For production use, use a cryptographically random string.
	 */
	std::string apiKey;

	/**
	 * @brief Path to file containing the API key.
	 *
	 * Alternative to setting apiKey directly. The file should contain
	 * the API key as plain text. Useful for separating secrets from
	 * configuration.
	 *
	 * Corresponds to: `--api-key-file FNAME`
	 */
	std::string apiKeyFile;

	/**
	 * @brief Request timeout in seconds.
	 *
	 * Maximum time to wait for a request to complete. Requests exceeding
	 * this timeout will be terminated.
	 *
	 * Corresponds to: `-to SECONDS` or `--timeout SECONDS`
	 * @default 600 (10 minutes)
	 */
	int timeout = 600;

	/**
	 * @brief Number of HTTP worker threads.
	 *
	 * Controls the thread pool size for handling HTTP requests.
	 * Negative values use an automatic heuristic.
	 *
	 * Corresponds to: `--threads-http N`
	 * @default -1 (auto)
	 */
	int threadsHttp = -1;

	/**
	 * @brief Reuse existing port if already bound.
	 *
	 * Allows binding to a port that is already in use by setting
	 * SO_REUSEADDR before binding.
	 *
	 * Corresponds to: `--reuse-port`
	 * @default false
	 */
	bool reusePort = false;

	/**
	 * @name SSL/TLS Settings
	 *
	 * These fields enable HTTPS by providing certificate and key files.
	 */

	/**
	 * @brief Path to SSL private key file.
	 *
	 * The private key for TLS encryption. Must be in PEM format.
	 * Required when sslCertFile is set.
	 *
	 * Corresponds to: `--ssl-key-file FNAME`
	 * @note Both sslKeyFile and sslCertFile must be set for HTTPS.
	 */
	std::string sslKeyFile;

	/**
	 * @brief Path to SSL certificate file.
	 *
	 * The certificate for TLS encryption. Must be in PEM format and
	 * match the private key in sslKeyFile.
	 *
	 * Corresponds to: `--ssl-cert-file FNAME`
	 * @note Both sslKeyFile and sslCertFile must be set for HTTPS.
	 */
	std::string sslCertFile;

	/**
	 * @name Static File Serving
	 *
	 * These fields enable serving static files from the filesystem.
	 */

	/**
	 * @brief Root path for static file serving.
	 *
	 * Files under this directory will be served at HTTP requests.
	 * Empty means static file serving is disabled.
	 *
	 * Corresponds to: `--path PATH`
	 */
	std::string path;

	/**
	 * @brief URL prefix for API endpoints.
	 *
	 * All API endpoints will be prefixed with this string.
	 * Useful for reverse proxy configurations.
	 *
	 * Corresponds to: `--api-prefix PREFIX`
	 */
	std::string apiPrefix;

	/**
	 * @brief Path for media file serving.
	 *
	 * Directory containing media files (images, audio) for
	 * multimodal model requests.
	 *
	 * Corresponds to: `--media-path PATH`
	 */
	std::string mediaPath;

	/**
	 * @name Server Behavior Settings
	 *
	 * These fields control server features and processing behavior.
	 */

	/**
	 * @brief Server alias for identification.
	 *
	 * A human-readable name for this server instance. Useful when
	 * running multiple servers.
	 *
	 * Corresponds to: `-a STRING` or `--alias STRING`
	 */
	std::string alias;

	/**
	 * @brief Enable the built-in web UI.
	 *
	 * When enabled, a web-based chat interface is available at
	 * the server's root URL.
	 *
	 * Corresponds to: `--ui` (enable) or `--no-ui` (disable)
	 * @default true
	 */
	bool ui = true;

	/**
	 * @brief Inline UI configuration.
	 *
	 * JSON configuration string for the web UI.
	 *
	 * Corresponds to: `--ui-config JSON`
	 */
	std::string uiConfig;

	/**
	 * @brief Path to UI configuration file.
	 *
	 * File containing JSON configuration for the web UI.
	 *
	 * Corresponds to: `--ui-config-file FNAME`
	 */
	std::string uiConfigFile;

	/**
	 * @brief Enable UI MCP proxy.
	 *
	 * Enables the Model Context Protocol proxy in the UI.
	 *
	 * Corresponds to: `--ui-mcp-proxy` (enable) or `--no-ui-mcp-proxy`
	 * (disable)
	 * @default false
	 */
	bool uiMcpProxy = false;

	/**
	 * @brief Enable tools endpoint.
	 *
	 * Enables the tools/calling endpoint for function calling.
	 * Value can be "all" or a comma-separated list of tool names.
	 *
	 * Corresponds to: `--tools TOOLS`
	 */
	std::string tools;

	/**
	 * @brief Run in embedding mode.
	 *
	 * When enabled, the server provides embedding endpoints instead
	 * of chat/completion endpoints.
	 *
	 * Corresponds to: `--embedding`
	 * @default false
	 */
	bool embedding = false;

	/**
	 * @brief Run in reranking mode.
	 *
	 * When enabled, the server provides reranking endpoints for
	 * search result reordering.
	 *
	 * Corresponds to: `--rerank`
	 * @default false
	 */
	bool reranking = false;

	/**
	 * @brief Enable continuous batching.
	 *
	 * Allows the server to interleave multiple requests for better
	 * throughput. Generally should be enabled.
	 *
	 * Corresponds to: `-cb` or `--cont-batching`
	 * @default true
	 */
	bool contBatching = true;

	/**
	 * @brief Enable prompt caching.
	 *
	 * Caches processed prompts for faster response to repeated requests.
	 *
	 * Corresponds to: `--cache-prompt`
	 * @default true
	 */
	bool cachePrompt = true;

	/**
	 * @brief Number of cached prompts to reuse.
	 *
	 * Controls the size of the prompt cache. Higher values use more
	 * memory but provide better cache hit rates.
	 *
	 * Corresponds to: `--cache-reuse N`
	 * @default 0
	 */
	int cacheReuse = 0;

	/**
	 * @brief Maximum host-memory prompt cache size in MiB.
	 *
	 * Caps the RAM held for cached prompt prefixes across slots. A value
	 * of 0 disables the host prompt cache; larger values raise the cache
	 * hit rate at the cost of system RAM.
	 *
	 * Corresponds to: `-cram N` or `--cache-ram N`
	 * @default 8192
	 * @range 0-1048576
	 */
	int cacheRam = 8192;

	/**
	 * @brief Maximum number of context checkpoints to create per slot.
	 *
	 * Context checkpoints let a slot roll back its KV cache to an earlier
	 * point instead of reprocessing the prompt. 0 disables checkpointing.
	 *
	 * Corresponds to: `-ctxcp N`, `--ctx-checkpoints N`,
	 * or `--swa-checkpoints N`
	 * @default 32
	 * @range 0-256
	 */
	int ctxCheckpoints = 32;

	/**
	 * @brief Enable context shift mode.
	 *
	 * Allows shifting context window for very long conversations.
	 *
	 * Corresponds to: `--context-shift`
	 * @default false
	 */
	bool contextShift = false;

	/**
	 * @brief Perform warmup on startup.
	 *
	 * Runs a dummy inference on startup to ensure model is loaded
	 * and ready, avoiding cold-start latency for first request.
	 *
	 * Corresponds to: `--warmup`
	 * @default true
	 */
	bool warmup = true;

	/**
	 * @brief Enable Jinja template support.
	 *
	 * Allows using Jinja2 templates for chat formatting.
	 *
	 * Corresponds to: `--jinja`
	 * @default true
	 */
	bool jinja = true;

	/**
	 * @brief Prefill assistant message in chat template.
	 *
	 * Automatically adds assistant message placeholder in chat
	 * templates for better streaming behavior.
	 *
	 * Corresponds to: `--prefill-assistant`
	 * @default true
	 */
	bool prefillAssistant = true;

	/**
	 * @brief Slot prompt similarity threshold.
	 *
	 * Minimum similarity score for reusing computation slots.
	 * Range: 0.0 to 1.0
	 *
	 * Corresponds to: `-sps VALUE`
	 * @default 0.1
	 * @range 0.0-1.0
	 */
	double slotPromptSimilarity = 0.1;

	/**
	 * @brief Idle timeout before server sleeps.
	 *
	 * Server will enter low-power mode after this many seconds
	 * of inactivity. Use -1 to disable.
	 *
	 * Corresponds to: `--sleep-idle-seconds SECONDS`
	 * @default -1 (disabled)
	 */
	int sleepIdleSeconds = -1;

	/**
	 * @name Endpoint Settings
	 *
	 * These fields control additional HTTP endpoints.
	 */

	/**
	 * @brief Enable metrics endpoint.
	 *
	 * Exposes Prometheus-format metrics at /metrics endpoint.
	 * Always enabled - this flag is not configurable.
	 *
	 * Corresponds to: `--metrics`
	 * @default true
	 */
	bool metrics = true;

	/**
	 * @brief Verbose API logging for debugging.
	 *
	 * When enabled, shows /models, /metrics, /slots API responses
	 * in the server log panel. Useful for debugging.
	 *
	 * @default false
	 */
	bool verboseApiLogs = false;

	/**
	 * @brief Enable properties endpoint.
	 *
	 * Exposes server properties and capabilities.
	 *
	 * Corresponds to: `--props`
	 * @default false
	 */
	bool props = false;

	/**
	 * @brief Enable slots endpoint.
	 *
	 * Exposes slot management endpoints for advanced control.
	 *
	 * Corresponds to: `--slots`
	 * @default true
	 */
	bool slots = true;

	/**
	 * @brief Path for saving slot state.
	 *
	 * Directory where slot state is persisted for recovery.
	 *
	 * Corresponds to: `--slot-save-path PATH`
	 */
	std::string slotSavePath;

	/**
	 * @brief Maximum number of models the router may keep loaded at once.
	 *
	 * Caps how many models the router holds in memory simultaneously.
	 *
	 * Corresponds to: `--models-max N`
	 * @default 4
	 * @range 1-64
	 */
	int modelsMax = 4;

	/**
	 * @brief Auto-load a model at server startup.
	 *
	 * When false (default), buildCommandArgs emits `--no-models-autoload`;
	 * when true it emits `--models-autoload`. Default is deliberately false:
	 * auto-loading the first/last models.ini entry after a crash-restart
	 * brings the just-crashed model straight back (#85). Only opt in if you
	 * understand that risk.
	 *
	 * Corresponds to: `--models-autoload` / `--no-models-autoload`
	 * @default false
	 */
	bool modelsAutoload = false;

	/**
	 * @brief Minimum spacing (in tokens) between automatic state checkpoints.
	 *
	 * Distinct from ctxCheckpoints (`--ctx-checkpoints`, a per-slot count):
	 * this is the minimum token interval enforced between checkpoints.
	 *
	 * Corresponds to: `-cms N` or `--checkpoint-min-step N`
	 * @note Renamed from `--checkpoint-every-n-tokens` (`-cpent`) in
	 *       llama.cpp b9842; older binaries do not accept the new flag.
	 * @default 8192
	 * @range 0-1048576
	 */
	int checkpointMinStep = 8192;

	/**
	 * @brief Cache idle slots' KV state instead of freeing it.
	 *
	 * Emits the positive or negative form depending on this flag.
	 *
	 * Corresponds to: `--cache-idle-slots` / `--no-cache-idle-slots`
	 * @default true
	 */
	bool cacheIdleSlots = true;

	/**
	 * @brief Pooling type for embedding outputs.
	 *
	 * One of {none, mean, cls, last, rank}. Empty omits the flag (server
	 * default). Invalid values are cleared to empty by validate().
	 *
	 * Corresponds to: `--pooling {none,mean,cls,last,rank}`
	 * @default "" (omitted)
	 */
	std::string pooling;

	/**
	 * @brief Embedding normalization order.
	 *
	 * Passed through to the server's `--embd-normalize`. -1 omits the flag
	 * (server default).
	 *
	 * Corresponds to: `--embd-normalize N`
	 * @default -1 (omitted)
	 */
	int embdNormalize = -1;

	/**
	 * @brief Reasoning-content handling mode.
	 *
	 * One of {on, off, auto}. Invalid values reset to "auto" by validate().
	 *
	 * Corresponds to: `-rea` or `--reasoning [on|off|auto]`
	 * @default "auto"
	 */
	std::string reasoning = "auto";

	/**
	 * @brief Token budget for reasoning content.
	 *
	 * -1 omits the flag (server default / unlimited).
	 *
	 * Corresponds to: `--reasoning-budget N`
	 * @default -1 (omitted)
	 */
	int reasoningBudget = -1;

	/**
	 * @brief Clamp numeric fields to valid ranges; apply SSL cross-field rule.
	 *
	 * Cross-field: if sslCertFile is set and sslKeyFile is empty,
	 * derives sslKeyFile = sslCertFile + ".key" to prevent silent SSL failure.
	 * Not noexcept — string assignment may throw std::bad_alloc.
	 */
	void validate();

	/**
	 * @brief Host to use when connecting to the server as a client.
	 *
	 * The `host` field is the bind address passed to llama-server via
	 * `--host`. A bind address of "0.0.0.0" (all interfaces) or empty is
	 * valid for binding but is NOT a connectable target, so any in-process
	 * client (health checks, /models, /metrics, ...) must dial loopback
	 * instead. This maps those bind-only values to "127.0.0.1" and returns
	 * `host` unchanged otherwise.
	 */
	[[nodiscard]] std::string connectHost() const
	{
		if (host.empty() || host == "0.0.0.0")
			return "127.0.0.1";
		return host;
	}
};

} // namespace Config
