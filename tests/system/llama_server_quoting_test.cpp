/**
 * @file llama_server_quoting_test.cpp
 * @brief Unit tests for LlamaServerProcess::quoteWindowsArg.
 *
 * The function is platform-neutral (pure string transform) so the
 * CommandLineToArgvW re-parsing rules can be verified on any platform.
 */

#include "llamaServerProcess.h"

#include <gtest/gtest.h>

TEST(QuoteWindowsArg, PlainArgUnchanged)
{
	EXPECT_EQ(LlamaServerProcess::quoteWindowsArg("--port"), "--port");
	EXPECT_EQ(LlamaServerProcess::quoteWindowsArg("8080"), "8080");
}

TEST(QuoteWindowsArg, EmptyArgQuoted)
{
	// An empty argument must survive as an empty token, not vanish.
	EXPECT_EQ(LlamaServerProcess::quoteWindowsArg(""), "\"\"");
}

TEST(QuoteWindowsArg, SpacesQuoted)
{
	EXPECT_EQ(LlamaServerProcess::quoteWindowsArg("C:\\Program Files\\llama"),
			  "\"C:\\Program Files\\llama\"");
}

TEST(QuoteWindowsArg, TrailingBackslashDoubled)
{
	// "D:\My Models\" must not end as \" (escaped quote that swallows the
	// rest of the command line).
	EXPECT_EQ(LlamaServerProcess::quoteWindowsArg("D:\\My Models\\"),
			  "\"D:\\My Models\\\\\"");
}

TEST(QuoteWindowsArg, BackslashesNotBeforeQuoteAreLiteral)
{
	// Interior backslashes (no following quote) stay single.
	EXPECT_EQ(LlamaServerProcess::quoteWindowsArg("a b\\c"), "\"a b\\c\"");
}

TEST(QuoteWindowsArg, EmbeddedQuoteEscaped)
{
	EXPECT_EQ(LlamaServerProcess::quoteWindowsArg("say \"hi\""),
			  "\"say \\\"hi\\\"\"");
}

TEST(QuoteWindowsArg, BackslashesBeforeQuoteDoubledAndQuoteEscaped)
{
	// One backslash before an embedded quote -> two backslashes + escaped
	// quote (3 backslashes total before the quote char).
	EXPECT_EQ(LlamaServerProcess::quoteWindowsArg("a\\\"b"),
			  "\"a\\\\\\\"b\"");
}

TEST(QuoteWindowsArg, BackslashOnlyArgWithoutWhitespaceUnchanged)
{
	// No whitespace and no quote: returned verbatim (CreateProcess parses it
	// as a single token already).
	EXPECT_EQ(LlamaServerProcess::quoteWindowsArg("D:\\Models\\x.gguf"),
			  "D:\\Models\\x.gguf");
}

// =============================================================================
// classifyProbeResponse — worker liveness from a proxied /slots outcome
// =============================================================================

using WorkerState = LlamaServerProcess::WorkerState;
using HttpResult = HttpClient::HttpResult;

TEST(ClassifyProbeResponse, OkIsAlive)
{
	EXPECT_EQ(LlamaServerProcess::classifyProbeResponse(HttpResult::OK, "[]"),
			  WorkerState::ALIVE);
}

TEST(ClassifyProbeResponse, ConnectErrorIsDead)
{
	EXPECT_EQ(LlamaServerProcess::classifyProbeResponse(
				  HttpResult::CONNECT_ERROR, "Connection refused"),
			  WorkerState::DEAD);
}

TEST(ClassifyProbeResponse, TimeoutIsBusy)
{
	EXPECT_EQ(LlamaServerProcess::classifyProbeResponse(HttpResult::TIMEOUT,
														"read timeout"),
			  WorkerState::BUSY);
}

TEST(ClassifyProbeResponse, RouterProxyErrorIsDead)
{
	// Observed in the wild: the router does NOT refuse the proxied
	// connection when its worker dies; it answers HTTP 500 itself. That body
	// is the router reporting it cannot reach the worker — a crash signal,
	// previously misclassified as BUSY (which blocked auto-restart forever).
	EXPECT_EQ(LlamaServerProcess::classifyProbeResponse(
				  HttpResult::OTHER_ERROR,
				  "HTTP 500: proxy error: Could not establish connection"),
			  WorkerState::DEAD);
}

TEST(ClassifyProbeResponse, OtherHttpErrorIsBusy)
{
	// Any other server-side error stays fail-safe BUSY: never restart on an
	// ambiguous probe.
	EXPECT_EQ(LlamaServerProcess::classifyProbeResponse(
				  HttpResult::OTHER_ERROR, "HTTP 503: loading model"),
			  WorkerState::BUSY);
}
