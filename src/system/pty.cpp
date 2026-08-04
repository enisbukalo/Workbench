/**
 * @file pty.cpp
 * @brief PTY handler platform dispatcher.
 *
 * Routes each public method to the appropriate Linux, Windows, or
 * unknown-platform implementation based on compile-time definitions.
 */

#include "ptyHandler.h"

bool PtyHandler::spawn(int cols, int rows)
{
#ifdef _WIN32
	return spawnWindows(cols, rows);
#elif __linux__
	return spawnLinux(cols, rows);
#else
	return spawnUnknown(cols, rows);
#endif
}

int PtyHandler::read(char *buf, std::size_t len)
{
#ifdef _WIN32
	return readWindows(buf, len);
#elif __linux__
	return readLinux(buf, len);
#else
	return readUnknown(buf, len);
#endif
}

int PtyHandler::write(const char *data, std::size_t len)
{
#ifdef _WIN32
	return writeWindows(data, len);
#elif __linux__
	return writeLinux(data, len);
#else
	return writeUnknown(data, len);
#endif
}

bool PtyHandler::resize(int cols, int rows)
{
#ifdef _WIN32
	return resizeWindows(cols, rows);
#elif __linux__
	return resizeLinux(cols, rows);
#else
	return resizeUnknown(cols, rows);
#endif
}

void PtyHandler::close()
{
#ifdef _WIN32
	closeWindows();
#elif __linux__
	closeLinux();
#else
	closeUnknown();
#endif
}

bool PtyHandler::isAlive()
{
#ifdef _WIN32
	return isAliveWindows();
#elif __linux__
	return isAliveLinux();
#else
	return isAliveUnknown();
#endif
}

PtyHandler::~PtyHandler()
{
	close();
}

// Unknown platform stubs
bool PtyHandler::spawnUnknown([[maybe_unused]] int cols,
							  [[maybe_unused]] int rows)
{
	return false;
}

int PtyHandler::readUnknown([[maybe_unused]] char *buf,
							[[maybe_unused]] std::size_t len)
{
	return -1;
}

int PtyHandler::writeUnknown([[maybe_unused]] const char *data,
							 [[maybe_unused]] std::size_t len)
{
	return -1;
}

bool PtyHandler::resizeUnknown([[maybe_unused]] int cols,
							   [[maybe_unused]] int rows)
{
	return false;
}

void PtyHandler::closeUnknown()
{
}

bool PtyHandler::isAliveUnknown()
{
	return false;
}
