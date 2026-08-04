#include "httpClient.h"
#include <gtest/gtest.h>

/**
 * @brief Tests for HttpClient.
 *
 * Tests the public API including URL parsing (exercised indirectly
 * through GET/POST calls that fail at connection), move semantics,
 * and timeout configuration.
 */

TEST(HttpClient, Get_ConnectionRefused)
{
	HttpClient client;
	client.setTimeout(1);
	auto [ok, body] = client.get("http://127.0.0.1:19999/health");
	EXPECT_FALSE(ok);
}

TEST(HttpClient, Get_HttpsUrlRejected)
{
	// No TLS support is compiled in, so https must fail fast as an invalid
	// URL rather than silently dialing plaintext to port 443.
	HttpClient client;
	client.setTimeout(1);
	auto [ok, body] = client.get("https://127.0.0.1:19999/health");
	EXPECT_FALSE(ok);
	EXPECT_NE(body.find("Invalid URL"), std::string::npos);
}

TEST(HttpClient, Get_NoPortDefaultsTo80)
{
	HttpClient client;
	client.setTimeout(1);
	auto [ok, body] = client.get("http://127.0.0.1/health");
	EXPECT_FALSE(ok);
}

TEST(HttpClient, Get_NoPathDefaultsToRoot)
{
	HttpClient client;
	client.setTimeout(1);
	auto [ok, body] = client.get("http://127.0.0.1:19999");
	EXPECT_FALSE(ok);
}

TEST(HttpClient, Post_ConnectionRefused)
{
	HttpClient client;
	client.setTimeout(1);
	auto [ok, body] =
		client.post("http://127.0.0.1:19999/api", R"({"key":"value"})");
	EXPECT_FALSE(ok);
}

TEST(HttpClient, SetTimeout_DoesNotCrash)
{
	HttpClient client;
	client.setTimeout(10);
	client.setTimeout(1);
	// No crash = success
}

TEST(HttpClient, MoveConstructor_Works)
{
	HttpClient client1;
	client1.setTimeout(5);
	HttpClient client2(std::move(client1));
	// client2 should be usable
	client2.setTimeout(1);
	auto [ok, body] = client2.get("http://127.0.0.1:19999/health");
	EXPECT_FALSE(ok);
}

TEST(HttpClient, MoveAssignment_Works)
{
	HttpClient client1;
	client1.setTimeout(5);
	HttpClient client2;
	client2 = std::move(client1);
	client2.setTimeout(1);
	auto [ok, body] = client2.get("http://127.0.0.1:19999/health");
	EXPECT_FALSE(ok);
}

// =============================================================================
// HttpClient::urlEncode Tests
//
// Model ids can contain spaces (e.g. "Qwen3.6-27B-Q8 MTP"); without encoding
// the ?model= query value is malformed and /metrics + /slots return empty,
// leaving the panel stuck at 0.0 tok/s.
// =============================================================================

TEST(HttpClient, UrlEncode_PassesThroughUnreserved)
{
	// RFC 3986 unreserved characters must not change.
	EXPECT_EQ(HttpClient::urlEncode("Qwen3.6-27B_Q8.v2~x"),
			  "Qwen3.6-27B_Q8.v2~x");
}

TEST(HttpClient, UrlEncode_EncodesSpace)
{
	EXPECT_EQ(HttpClient::urlEncode("Qwen3.6-27B-Q8 MTP"),
			  "Qwen3.6-27B-Q8%20MTP");
}

TEST(HttpClient, UrlEncode_EncodesReservedChars)
{
	// Reserved/query-significant characters become %XX (uppercase hex).
	EXPECT_EQ(HttpClient::urlEncode("a&b=c?d/e"), "a%26b%3Dc%3Fd%2Fe");
}

TEST(HttpClient, UrlEncode_EmptyStaysEmpty)
{
	EXPECT_EQ(HttpClient::urlEncode(""), "");
}
