#include "ptyHandler.h"
#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <thread>

/**
 * @brief Tests for PtyHandler.
 *
 * Tests PTY spawn, read/write, resize, close, and lifecycle
 * operations. Runs on Linux (Docker) using forkpty().
 */

TEST(PtyHandler, Spawn_Success)
{
	PtyHandler pty;
	EXPECT_TRUE(pty.spawn(80, 24));
	EXPECT_TRUE(pty.isAlive());
	pty.close();
}

TEST(PtyHandler, Spawn_DoubleFails)
{
	PtyHandler pty;
	EXPECT_TRUE(pty.spawn(80, 24));
	EXPECT_FALSE(pty.spawn(80, 24));
	pty.close();
}

TEST(PtyHandler, WriteRead_BasicEcho)
{
	PtyHandler pty;
	ASSERT_TRUE(pty.spawn(80, 24));

	// Give shell time to start
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	// Write a command
	const char *cmd = "echo HELLO_PTY_TEST\n";
	int written = pty.write(cmd, std::strlen(cmd));
	EXPECT_GT(written, 0);

	// Wait for output
	std::this_thread::sleep_for(std::chrono::milliseconds(300));

	char buf[4096] = {};
	int totalRead = 0;
	// Read in a loop since output may arrive in chunks
	for (int i = 0; i < 10; ++i) {
		int n = pty.read(buf + totalRead, sizeof(buf) - totalRead - 1);
		if (n > 0)
			totalRead += n;
		if (totalRead > 0 &&
			std::string(buf).find("HELLO_PTY_TEST") != std::string::npos)
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	std::string output(buf, totalRead);
	EXPECT_NE(output.find("HELLO_PTY_TEST"), std::string::npos);
	pty.close();
}

TEST(PtyHandler, Resize_AfterSpawn)
{
	PtyHandler pty;
	ASSERT_TRUE(pty.spawn(80, 24));
	EXPECT_TRUE(pty.resize(120, 40));
	pty.close();
}

TEST(PtyHandler, Resize_BeforeSpawn_Fails)
{
	PtyHandler pty;
	EXPECT_FALSE(pty.resize(80, 24));
}

TEST(PtyHandler, Close_Idempotent)
{
	PtyHandler pty;
	ASSERT_TRUE(pty.spawn(80, 24));
	pty.close();
	// Second close should not crash
	pty.close();
	EXPECT_FALSE(pty.isAlive());
}

TEST(PtyHandler, IsAlive_AfterClose_ReturnsFalse)
{
	PtyHandler pty;
	ASSERT_TRUE(pty.spawn(80, 24));
	EXPECT_TRUE(pty.isAlive());
	pty.close();
	EXPECT_FALSE(pty.isAlive());
}

TEST(PtyHandler, Destructor_ClosesCleanly)
{
	{
		PtyHandler pty;
		ASSERT_TRUE(pty.spawn(80, 24));
		// Destructor should close without crash
	}
}

TEST(PtyHandler, IsAlive_BeforeSpawn_ReturnsFalse)
{
	PtyHandler pty;
	EXPECT_FALSE(pty.isAlive());
}

TEST(PtyHandler, Read_BeforeSpawn_ReturnsNegativeOrZero)
{
	PtyHandler pty;
	char buf[64];
	int n = pty.read(buf, sizeof(buf));
	EXPECT_LE(n, 0);
}

TEST(PtyHandler, Write_BeforeSpawn_ReturnsNegativeOrZero)
{
	PtyHandler pty;
	const char *data = "test";
	int n = pty.write(data, 4);
	EXPECT_LE(n, 0);
}
