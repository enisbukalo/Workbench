/**
 * @file ptyLinux.cpp
 * @brief Linux-specific PTY implementation using forkpty().
 *
 * Spawns a shell process via forkpty and provides non-blocking read/write
 * access to the master side of the pseudo-terminal.
 */

#include "ptyHandler.h"

#include <cstdlib>
#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <spdlog/spdlog.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

bool PtyHandler::spawnLinux(int cols, int rows)
{
	std::lock_guard<std::mutex> lock(ptyMutex_);

	if (alive_)
		return false;

	struct winsize ws = {};
	ws.ws_col = static_cast<unsigned short>(cols);
	ws.ws_row = static_cast<unsigned short>(rows);

	int master = -1;
	pid_t pid = forkpty(&master, nullptr, nullptr, &ws);

	if (pid < 0) {
		spdlog::error("PTY: forkpty failed (errno: {})", errno);
		return false;
	}

	if (pid == 0) {
		// Child process: exec a shell
		const char *shell = std::getenv("SHELL");
		if (!shell)
			shell = "/bin/sh";
		execlp(shell, shell, nullptr);
		_exit(127);
	}

	// Parent: set master fd to non-blocking
	int flags = fcntl(master, F_GETFL, 0);
	if (flags != -1)
		fcntl(master, F_SETFL, flags | O_NONBLOCK);

	masterFd_ = master;
	childPid_ = pid;
	alive_ = true;
	spdlog::info("PTY: spawned (pid: {})", pid);
	return true;
}

int PtyHandler::readLinux(char *buf, std::size_t len)
{
	std::lock_guard<std::mutex> lock(ptyMutex_);

	if (!alive_ || masterFd_ < 0)
		return -1;

	ssize_t n = ::read(masterFd_, buf, len);
	if (n < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return 0;
		return -1;
	}
	return static_cast<int>(n);
}

int PtyHandler::writeLinux(const char *data, std::size_t len)
{
	std::lock_guard<std::mutex> lock(ptyMutex_);

	if (!alive_ || masterFd_ < 0)
		return -1;

	ssize_t n = ::write(masterFd_, data, len);
	return (n < 0) ? -1 : static_cast<int>(n);
}

bool PtyHandler::resizeLinux(int cols, int rows)
{
	std::lock_guard<std::mutex> lock(ptyMutex_);

	if (!alive_ || masterFd_ < 0)
		return false;

	struct winsize ws = {};
	ws.ws_col = static_cast<unsigned short>(cols);
	ws.ws_row = static_cast<unsigned short>(rows);

	return ioctl(masterFd_, TIOCSWINSZ, &ws) == 0;
}

void PtyHandler::closeLinux()
{
	std::lock_guard<std::mutex> lock(ptyMutex_);

	if (!alive_)
		return;

	if (masterFd_ >= 0) {
		::close(masterFd_);
		masterFd_ = -1;
	}

	if (childPid_ > 0) {
		int status;
		waitpid(childPid_, &status, 0);
		childPid_ = -1;
	}

	alive_ = false;
	spdlog::debug("PTY: closed");
}

bool PtyHandler::isAliveLinux()
{
	std::lock_guard<std::mutex> lock(ptyMutex_);

	if (!alive_ || childPid_ <= 0)
		return false;

	int status;
	pid_t result = waitpid(childPid_, &status, WNOHANG);

	if (result == 0) {
		// Child still running
		return true;
	}

	// Child has exited
	alive_ = false;
	if (masterFd_ >= 0) {
		::close(masterFd_);
		masterFd_ = -1;
	}
	childPid_ = -1;
	return false;
}
