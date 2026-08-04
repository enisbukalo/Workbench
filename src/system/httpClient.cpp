#include "httpClient.h"
#include "server/httplib.h"
#include <chrono>
#include <spdlog/spdlog.h>
#include <utility>

class HttpClient::Impl
{
  public:
	Impl() : m_timeout(5)
	{
	}

	std::pair<bool, std::string> get(std::string_view url)
	{
		// Delegate to the classifying variant so there is a single GET path;
		// collapse the classification back to a bool for existing callers.
		auto [result, body] = getWithResult(url);
		return { result == HttpClient::HttpResult::OK, body };
	}

	std::pair<HttpClient::HttpResult, std::string>
	getWithResult(std::string_view url)
	{
		using HttpResult = HttpClient::HttpResult;
		try {
			// Create client for this request (host extracted from URL)
			auto [host, port, path] = parseUrl(url);
			if (host.empty()) {
				return { HttpResult::OTHER_ERROR,
						 "Invalid URL: could not parse host" };
			}

			httplib::Client cli(host.c_str(), port);
			cli.set_connection_timeout(m_timeout);
			cli.set_read_timeout(m_timeout);

			auto response = cli.Get(path.data());

			if (!response) {
				// Read the httplib error kind before the Result is discarded so
				// the caller can tell a refused worker (crash) from a slow one
				// (busy). This is the whole reason getWithResult exists.
				HttpResult kind = mapHttplibError(response.error());
				std::string err = httplib::to_string(response.error());
				spdlog::debug("HTTP GET {} failed: {}", url, err);
				return { kind, err };
			}
			if (response->status != 200) {
				std::string err = "HTTP " + std::to_string(response->status);
				// Include response body for error details
				if (!response->body.empty()) {
					err += ": " + response->body;
				}
				spdlog::debug("HTTP GET {} failed: {}", url, err);
				return { HttpResult::OTHER_ERROR, err };
			}

			return { HttpResult::OK, response->body };
		} catch (const std::exception &e) {
			spdlog::debug("HTTP GET {} exception: {}", url, e.what());
			return { HttpResult::OTHER_ERROR, e.what() };
		}
	}

	std::pair<bool, std::string> post(std::string_view url,
									  std::string_view jsonBody)
	{
		try {
			auto [host, port, path] = parseUrl(url);
			if (host.empty()) {
				return { false, "Invalid URL: could not parse host" };
			}

			httplib::Client cli(host.c_str(), port);
			cli.set_connection_timeout(m_timeout);
			cli.set_read_timeout(m_timeout);
			cli.set_write_timeout(m_timeout);

			auto response =
				cli.Post(path.data(), jsonBody.data(), "application/json");

			if (!response) {
				spdlog::debug("HTTP POST {} failed: No response", url);
				return { false, "No response" };
			}
			if (response->status != 200 && response->status != 201) {
				std::string err = "HTTP " + std::to_string(response->status);
				// Include response body for error details
				if (!response->body.empty()) {
					err += ": " + response->body;
				}
				spdlog::debug("HTTP POST {} failed: {}", url, err);
				return { false, err };
			}

			return { true, response->body };
		} catch (const std::exception &e) {
			spdlog::debug("HTTP POST {} exception: {}", url, e.what());
			return { false, e.what() };
		}
	}

	void setTimeout(int timeoutSeconds)
	{
		m_timeout = timeoutSeconds;
	}

  private:
	// Map an httplib transport error to the coarse HttpResult the crash
	// detector reasons about. A refused/closed connection means the worker
	// process is gone (real crash, #18912); a read/connection timeout means it
	// is alive but slow (busy decode). Anything else is treated as ambiguous.
	static HttpClient::HttpResult mapHttplibError(httplib::Error error)
	{
		using HttpResult = HttpClient::HttpResult;
		switch (error) {
		case httplib::Error::Connection:
		case httplib::Error::ConnectionTimeout:
		case httplib::Error::ConnectionClosed:
			return HttpResult::CONNECT_ERROR;
		case httplib::Error::Read:
		case httplib::Error::Write:
		case httplib::Error::Timeout:
			return HttpResult::TIMEOUT;
		default:
			return HttpResult::OTHER_ERROR;
		}
	}

	// Simple URL parser - extracts host, port, and path from full URL
	// Expected format: http://host:port/path or http://host/path
	static std::tuple<std::string, int, std::string>
	parseUrl(std::string_view url)
	{
		std::string host;
		int port = 80;
		std::string path = "/";

		// Skip "http://". https is rejected up front: httplib is compiled
		// without TLS here, so a https URL would silently go out as
		// plaintext to port 443 — failing loudly beats a fake-secure call.
		if (url.starts_with("http://")) {
			url = url.substr(7);
		} else if (url.starts_with("https://")) {
			return { "", 0, "/" }; // empty host -> caller reports invalid URL
		}

		// Find first / to separate host:port from path
		auto slashPos = url.find('/');
		if (slashPos == std::string_view::npos) {
			host = std::string(url);
			path = "/";
		} else {
			host = std::string(url.substr(0, slashPos));
			path = std::string(url.substr(slashPos));
		}

		// Extract port from host if present (e.g., "127.0.0.1:8080")
		auto colonPos = host.rfind(':');
		if (colonPos != std::string::npos) {
			// Check if what follows colon is digits (port number)
			bool isPort = true;
			for (size_t i = colonPos + 1; i < host.size(); ++i) {
				// Cast: std::isdigit is UB for negative char values.
				if (!std::isdigit(static_cast<unsigned char>(host[i]))) {
					isPort = false;
					break;
				}
			}
			if (isPort) {
				std::string portStr = host.substr(colonPos + 1);
				host = host.substr(0, colonPos);
				try {
					port = std::stoi(portStr);
				} catch (...) {
					port = 80;
				}
			}
		}

		return { host, port, path };
	}

	int m_timeout;
};

// Implementation of HttpClient methods
HttpClient::HttpClient() noexcept : m_impl(std::make_unique<Impl>())
{
}

HttpClient::~HttpClient() = default;

HttpClient::HttpClient(HttpClient &&) noexcept = default;
HttpClient &HttpClient::operator=(HttpClient &&) noexcept = default;

std::pair<bool, std::string> HttpClient::get(std::string_view url)
{
	return m_impl->get(url);
}

std::pair<HttpClient::HttpResult, std::string>
HttpClient::getWithResult(std::string_view url)
{
	return m_impl->getWithResult(url);
}

std::pair<bool, std::string> HttpClient::post(std::string_view url,
											  std::string_view jsonBody)
{
	return m_impl->post(url, jsonBody);
}

void HttpClient::setTimeout(int timeoutSeconds)
{
	m_impl->setTimeout(timeoutSeconds);
}

std::string HttpClient::urlEncode(std::string_view value)
{
	std::string encoded;
	encoded.reserve(value.size());
	for (unsigned char c : value) {
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
			(c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
			c == '~') {
			encoded.push_back(static_cast<char>(c));
		} else {
			static constexpr char hex[] = "0123456789ABCDEF";
			encoded.push_back('%');
			encoded.push_back(hex[c >> 4]);
			encoded.push_back(hex[c & 0x0F]);
		}
	}
	return encoded;
}