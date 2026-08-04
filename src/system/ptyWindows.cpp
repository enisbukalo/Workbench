/**
 * @file ptyWindows.cpp
 * @brief Windows-specific PTY implementation using ConPTY.
 *
 * Spawns a shell process via CreatePseudoConsole and provides
 * read/write access through named pipes.
 */

#include "ptyHandler.h"

#include <spdlog/spdlog.h>

// ConPTY requires Windows 10 1809+; ensure the SDK version macros are set
// so that MinGW headers expose CreatePseudoConsole and friends.
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000006
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>

bool PtyHandler::spawnWindows(int cols, int rows)
{
	std::lock_guard<std::mutex> lock(ptyMutex_);

	if (alive_)
		return false;

	HANDLE pipePtyIn = INVALID_HANDLE_VALUE;
	HANDLE pipePtyOut = INVALID_HANDLE_VALUE;
	HANDLE pipeAppIn = INVALID_HANDLE_VALUE;
	HANDLE pipeAppOut = INVALID_HANDLE_VALUE;

	// Create pipes: app writes to pipePtyIn, reads from pipeAppOut
	if (!CreatePipe(&pipePtyIn, &pipeAppIn, nullptr, 0)) {
		spdlog::error("PTY: failed to create pipes");
		return false;
	}

	if (!CreatePipe(&pipeAppOut, &pipePtyOut, nullptr, 0)) {
		CloseHandle(pipePtyIn);
		CloseHandle(pipeAppIn);
		spdlog::error("PTY: failed to create pipes");
		return false;
	}

	// Create the pseudo console
	COORD size;
	size.X = static_cast<SHORT>(cols);
	size.Y = static_cast<SHORT>(rows);

	HPCON hpc = nullptr;
	HRESULT hr = CreatePseudoConsole(size, pipePtyIn, pipePtyOut, 0, &hpc);

	// Close the PTY-side pipe handles; ConPTY owns them now
	CloseHandle(pipePtyIn);
	CloseHandle(pipePtyOut);

	if (FAILED(hr)) {
		CloseHandle(pipeAppIn);
		CloseHandle(pipeAppOut);
		spdlog::error("PTY: CreatePseudoConsole failed (hr: {})",
					  static_cast<int>(hr));
		return false;
	}

	// Set up process attributes with the pseudo console
	SIZE_T attrSize = 0;
	InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);

	auto attrList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
		HeapAlloc(GetProcessHeap(), 0, attrSize));

	if (!attrList) {
		ClosePseudoConsole(hpc);
		CloseHandle(pipeAppIn);
		CloseHandle(pipeAppOut);
		return false;
	}

	if (!InitializeProcThreadAttributeList(attrList, 1, 0, &attrSize)) {
		HeapFree(GetProcessHeap(), 0, attrList);
		ClosePseudoConsole(hpc);
		CloseHandle(pipeAppIn);
		CloseHandle(pipeAppOut);
		return false;
	}

	if (!UpdateProcThreadAttribute(attrList,
								   0,
								   PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
								   hpc,
								   sizeof(HPCON),
								   nullptr,
								   nullptr)) {
		DeleteProcThreadAttributeList(attrList);
		HeapFree(GetProcessHeap(), 0, attrList);
		ClosePseudoConsole(hpc);
		CloseHandle(pipeAppIn);
		CloseHandle(pipeAppOut);
		return false;
	}

	// Launch the shell process
	STARTUPINFOEXW si = {};
	si.StartupInfo.cb = sizeof(STARTUPINFOEXW);
	si.lpAttributeList = attrList;

	PROCESS_INFORMATION pi = {};
	wchar_t cmd[] = L"powershell.exe";

	BOOL ok = CreateProcessW(nullptr,
							 cmd,
							 nullptr,
							 nullptr,
							 FALSE,
							 EXTENDED_STARTUPINFO_PRESENT,
							 nullptr,
							 nullptr,
							 &si.StartupInfo,
							 &pi);

	DeleteProcThreadAttributeList(attrList);
	HeapFree(GetProcessHeap(), 0, attrList);

	if (!ok) {
		DWORD error = GetLastError();
		ClosePseudoConsole(hpc);
		CloseHandle(pipeAppIn);
		CloseHandle(pipeAppOut);
		spdlog::error("PTY: CreateProcessW failed (error: {})",
					  static_cast<int>(error));
		return false;
	}

	spdlog::info("PTY: spawned (PID: {})", pi.dwProcessId);

	CloseHandle(pi.hThread);

	hpc_ = hpc;
	pipeIn_ = pipeAppIn;
	pipeOut_ = pipeAppOut;
	procHandle_ = pi.hProcess;
	alive_ = true;
	return true;
}

int PtyHandler::readWindows(char *buf, std::size_t len)
{
	std::lock_guard<std::mutex> lock(ptyMutex_);

	if (!alive_ || pipeOut_ == nullptr)
		return -1;

	DWORD available = 0;
	if (!PeekNamedPipe(static_cast<HANDLE>(pipeOut_),
					   nullptr,
					   0,
					   nullptr,
					   &available,
					   nullptr)) {
		return -1;
	}

	if (available == 0)
		return 0;

	DWORD toRead = (available < static_cast<DWORD>(len))
					   ? available
					   : static_cast<DWORD>(len);
	DWORD bytesRead = 0;

	if (!ReadFile(static_cast<HANDLE>(pipeOut_),
				  buf,
				  toRead,
				  &bytesRead,
				  nullptr)) {
		return -1;
	}

	return static_cast<int>(bytesRead);
}

int PtyHandler::writeWindows(const char *data, std::size_t len)
{
	std::lock_guard<std::mutex> lock(ptyMutex_);

	if (!alive_ || pipeIn_ == nullptr)
		return -1;

	DWORD bytesWritten = 0;
	if (!WriteFile(static_cast<HANDLE>(pipeIn_),
				   data,
				   static_cast<DWORD>(len),
				   &bytesWritten,
				   nullptr)) {
		return -1;
	}

	return static_cast<int>(bytesWritten);
}

bool PtyHandler::resizeWindows(int cols, int rows)
{
	std::lock_guard<std::mutex> lock(ptyMutex_);

	if (!alive_ || hpc_ == nullptr)
		return false;

	COORD size;
	size.X = static_cast<SHORT>(cols);
	size.Y = static_cast<SHORT>(rows);

	HRESULT hr = ResizePseudoConsole(static_cast<HPCON>(hpc_), size);
	return SUCCEEDED(hr);
}

void PtyHandler::closeWindows()
{
	std::lock_guard<std::mutex> lock(ptyMutex_);

	// Keyed on the handles, NOT on alive_: isAliveWindows() flips alive_ to
	// false when the shell exits but leaves the handles open for this method
	// to reclaim. An alive_ early-out here leaked the ConPTY, both pipes and
	// the process handle on every shell exit.
	if (hpc_) {
		ClosePseudoConsole(static_cast<HPCON>(hpc_));
		hpc_ = nullptr;
	}

	if (procHandle_) {
		// Give the process a moment to exit gracefully (skip the wait when
		// isAliveWindows() already saw it exit)
		if (alive_ && WaitForSingleObject(static_cast<HANDLE>(procHandle_),
										  500) != WAIT_OBJECT_0) {
			TerminateProcess(static_cast<HANDLE>(procHandle_), 1);
		}
		CloseHandle(static_cast<HANDLE>(procHandle_));
		procHandle_ = nullptr;
	}

	if (pipeIn_) {
		CloseHandle(static_cast<HANDLE>(pipeIn_));
		pipeIn_ = nullptr;
	}

	if (pipeOut_) {
		CloseHandle(static_cast<HANDLE>(pipeOut_));
		pipeOut_ = nullptr;
	}

	alive_ = false;
	spdlog::debug("PTY: closed");
}

bool PtyHandler::isAliveWindows()
{
	std::lock_guard<std::mutex> lock(ptyMutex_);

	if (!alive_ || procHandle_ == nullptr)
		return false;

	DWORD result = WaitForSingleObject(static_cast<HANDLE>(procHandle_), 0);

	if (result == WAIT_OBJECT_0) {
		// Process has exited. Only the flag flips here; the ConPTY, pipe and
		// process handles stay open until close() reclaims them (close() is
		// keyed on the handles, not on alive_).
		alive_ = false;
		return false;
	}

	return true;
}
