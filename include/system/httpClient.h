#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <utility>

/**
 * @class HttpClient
 * @brief Minimal HTTP client for llama-server API calls.
 *
 * Thread-safe, uses httplib library under the hood.
 */
class HttpClient
{
  public:
	/**
	 * @brief Classified outcome of an HTTP request.
	 *
	 * Lets callers distinguish a refused/closed connection (the remote isn't
	 * there — e.g. a crashed router-mode worker) from a read/connection timeout
	 * (the remote is there but slow — e.g. a worker busy with heavy decode). The
	 * crash detector relies on this distinction to avoid restarting a healthy
	 * but busy server. Keeps httplib out of the public interface.
	 */
	enum class HttpResult
	{
		OK,			   ///< HTTP 200, body returned.
		CONNECT_ERROR, ///< Connection refused/closed — remote unreachable.
		TIMEOUT,	   ///< Connect/read/write timed out — remote slow.
		OTHER_ERROR	   ///< Non-200, invalid URL, exception, or unknown error.
	};

	HttpClient() noexcept;
	~HttpClient();

	// Non-copyable
	HttpClient(const HttpClient &) = delete;
	HttpClient &operator=(const HttpClient &) = delete;

	// Movable
	HttpClient(HttpClient &&) noexcept;
	HttpClient &operator=(HttpClient &&) noexcept;

	/**
	 * @brief Perform GET request.
	 * @param url Full URL (e.g., "http://127.0.0.1:8080/health")
	 * @return Pair of (success, response body or error message)
	 */
	[[nodiscard]] std::pair<bool, std::string> get(std::string_view url);

	/**
	 * @brief Perform GET request, classifying the failure kind.
	 *
	 * Same request as get(), but the first element is an @ref HttpResult so the
	 * caller can tell a refused connection (CONNECT_ERROR) from a timeout
	 * (TIMEOUT). On OK the second element is the response body; otherwise it is
	 * a human-readable error string for logging.
	 *
	 * @param url Full URL (e.g., "http://127.0.0.1:8080/slots?model=foo")
	 * @return Pair of (classified result, response body or error message)
	 */
	[[nodiscard]] std::pair<HttpResult, std::string>
	getWithResult(std::string_view url);

	/**
	 * @brief Perform POST request with JSON body.
	 * @param url Full URL
	 * @param jsonBody JSON payload
	 * @return Pair of (success, response body or error message)
	 */
	[[nodiscard]] std::pair<bool, std::string> post(std::string_view url,
													std::string_view jsonBody);

	/**
	 * @brief Set the timeout in seconds.
	 * @param timeoutSeconds Timeout value
	 */
	void setTimeout(int timeoutSeconds);

	/**
	 * @brief Percent-encode a string for use as a URL query value.
	 *
	 * Model ids may contain spaces and other reserved characters (e.g.
	 * "Qwen3.6-27B-Q8 MTP"); pasting them raw into a query string produces a
	 * malformed URL that the server cannot match, so /metrics and /slots come
	 * back empty. Unreserved characters (RFC 3986: A-Z a-z 0-9 - _ . ~) pass
	 * through; everything else becomes %XX.
	 *
	 * @param value Raw query value (e.g. a model id).
	 * @return Percent-encoded value safe to append after "?model=".
	 */
	[[nodiscard]] static std::string urlEncode(std::string_view value);

  private:
	class Impl;
	std::unique_ptr<Impl> m_impl;
};