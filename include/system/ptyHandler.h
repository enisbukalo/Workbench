#pragma once
#include <cstddef>
#include <mutex>

/**
 * @file ptyHandler.h
 * @brief Thread-safe class that manages a pseudo-terminal (PTY).
 *
 * This class provides a unified interface for pseudo-terminal management
 * across platforms:
 * - Windows: Uses ConPTY (CreatePseudoConsole, UpdateProcThreadAttribute)
 * - Linux: Uses forkpty() from libutil
 * - Other platforms: Stub implementations that return failure
 *
 * The PTY is used to spawn shell processes and communicate with them
 * via non-blocking read/write operations. The master fd (Linux) or
 * pipe handles (Windows) are managed internally.
 *
 * Thread model:
 * - All public methods are thread-safe via ptyMutex_
 * - Platform-specific implementations also acquire ptyMutex_ internally
 * - PTY operations (spawn, read, write, resize, close) are serialized
 */
class PtyHandler
{
  public:
	/**
	 * @brief Default constructor.
	 *
	 * Creates an unspawned PtyHandler instance. The PTY must be spawned
	 * via spawn() before any read/write operations can be performed.
	 */
	PtyHandler() = default;

	/**
	 * @brief Spawns a new shell process in a PTY of the given size.
	 *
	 * This method:
	 * 1. Acquires ptyMutex_ to ensure thread safety
	 * 2. Checks that the PTY is not already spawned
	 * 3. Creates a new PTY with the specified dimensions
	 * 4. Forks/execs a shell process (bash/sh on Linux, cmd.exe on Windows)
	 * 5. Sets the master fd/pipe to non-blocking mode
	 * 6. Stores the PTY file descriptor or handles
	 *
	 * @param cols The desired terminal width in columns.
	 * @param rows The desired terminal height in rows.
	 * @return true on success, false if the PTY is already spawned or
	 *         the spawn operation failed.
	 * @note The shell process inherits the PTY as its standard input/output.
	 * @note On Linux, the shell is determined by the SHELL environment
	 *       variable, defaulting to /bin/sh.
	 */
	bool spawn(int cols, int rows);

	/**
	 * @brief Reads available output from the PTY into @p buf.
	 *
	 * This method performs a non-blocking read from the PTY master fd
	 * (Linux) or pipe (Windows). If no data is available, it returns 0
	 * immediately without blocking.
	 *
	 * @param buf Buffer to store the read data. Must be at least len bytes.
	 * @param len Maximum number of bytes to read.
	 * @return Number of bytes read (0 if no data available, -1 on error).
	 * @note This method is non-blocking; it returns immediately if no
	 *       data is available.
	 * @note Callers should check isAlive() to detect if the shell has
	 *       exited.
	 */
	int read(char *buf, std::size_t len);

	/**
	 * @brief Writes @p len bytes of @p data to the PTY.
	 *
	 * This method writes data to the PTY master fd (Linux) or pipe
	 * (Windows), which is received by the shell process as standard input.
	 *
	 * @param data Pointer to the data to write.
	 * @param len Number of bytes to write.
	 * @return Number of bytes written, or -1 on error.
	 * @note This method may block if the PTY buffer is full.
	 * @note Special characters (e.g., Ctrl+C, Ctrl+D) are passed through
	 *       to the shell.
	 */
	int write(const char *data, std::size_t len);

	/**
	 * @brief Resizes the PTY to the given dimensions.
	 *
	 * This method sends a TIOCSWINSZ ioctl (Linux) or calls
	 * ResizePseudoConsole (Windows) to notify the shell of the new
	 * terminal dimensions.
	 *
	 * @param cols The new terminal width in columns.
	 * @param rows The new terminal height in rows.
	 * @return true on success, false if the PTY is not spawned or
	 *         the resize operation failed.
	 * @note The shell may need to respond to the resize by redrawing
	 *       its prompt or reflowing wrapped lines.
	 */
	bool resize(int cols, int rows);

	/**
	 * @brief Closes the PTY and terminates the child process.
	 *
	 * This method:
	 * 1. Closes the master fd (Linux) or pipe handles (Windows)
	 * 2. Waits for the child process to exit (Linux) or terminates it
	 *    if it doesn't exit within 500ms (Windows)
	 * 3. Resets the PTY state
	 *
	 * @note Safe to call multiple times; subsequent calls are no-ops.
	 * @note The PTY cannot be respawned after close(); a new PtyHandler
	 *       instance must be created.
	 */
	void close();

	/**
	 * @brief Checks whether the child process is still running.
	 *
	 * This method uses waitpid with WNOHANG (Linux) or
	 * WaitForSingleObject with zero timeout (Windows) to check if the
	 * shell process has exited without blocking.
	 *
	 * @return true if the child process is still running, false if it
	 *         has exited or the PTY is not spawned.
	 * @note This method is safe to call from any thread.
	 */
	bool isAlive();

	/**
	 * @brief Destructor that closes the PTY.
	 *
	 * Calls close() to ensure the PTY is properly cleaned up when the
	 * PtyHandler instance is destroyed.
	 * @note Safe to call multiple times; close() is idempotent.
	 */
	~PtyHandler();

  private:
	/**
	 * @brief Deleted copy constructor — PTY state cannot be duplicated.
	 *
	 * Each PtyHandler instance owns a unique PTY that cannot be shared.
	 */
	PtyHandler(const PtyHandler &) = delete;

	/**
	 * @brief Deleted copy-assignment operator — PTY state cannot be duplicated.
	 *
	 * Each PtyHandler instance owns a unique PTY that cannot be shared.
	 */
	PtyHandler &operator=(const PtyHandler &) = delete;

	mutable std::mutex ptyMutex_;
	bool alive_ = false;

	// Platform-specific dispatch targets
	/**
	 * @brief Linux-specific PTY spawn implementation using forkpty().
	 */
	bool spawnLinux(int cols, int rows);

	/**
	 * @brief Windows-specific PTY spawn implementation using ConPTY.
	 */
	bool spawnWindows(int cols, int rows);

	/**
	 * @brief Fallback implementation for unknown platforms.
	 */
	bool spawnUnknown(int cols, int rows);

	/**
	 * @brief Linux-specific read implementation using read().
	 */
	int readLinux(char *buf, std::size_t len);

	/**
	 * @brief Windows-specific read implementation using ReadFile().
	 */
	int readWindows(char *buf, std::size_t len);

	/**
	 * @brief Fallback implementation for unknown platforms.
	 */
	int readUnknown(char *buf, std::size_t len);

	/**
	 * @brief Linux-specific write implementation using write().
	 */
	int writeLinux(const char *data, std::size_t len);

	/**
	 * @brief Windows-specific write implementation using WriteFile().
	 */
	int writeWindows(const char *data, std::size_t len);

	/**
	 * @brief Fallback implementation for unknown platforms.
	 */
	int writeUnknown(const char *data, std::size_t len);

	/**
	 * @brief Linux-specific resize implementation using TIOCSWINSZ ioctl.
	 */
	bool resizeLinux(int cols, int rows);

	/**
	 * @brief Windows-specific resize implementation using ResizePseudoConsole.
	 */
	bool resizeWindows(int cols, int rows);

	/**
	 * @brief Fallback implementation for unknown platforms.
	 */
	bool resizeUnknown(int cols, int rows);

	/**
	 * @brief Linux-specific close implementation using close() and waitpid().
	 */
	void closeLinux();

	/**
	 * @brief Windows-specific close implementation using CloseHandle and
	 * TerminateProcess.
	 */
	void closeWindows();

	/**
	 * @brief Fallback implementation for unknown platforms.
	 */
	void closeUnknown();

	/**
	 * @brief Linux-specific isAlive implementation using waitpid(WNOHANG).
	 */
	bool isAliveLinux();

	/**
	 * @brief Windows-specific isAlive implementation using WaitForSingleObject.
	 */
	bool isAliveWindows();

	/**
	 * @brief Fallback implementation for unknown platforms.
	 */
	bool isAliveUnknown();

#ifdef _WIN32
	/**
	 * @brief Windows-specific: ConPTY handle.
	 *
	 * Opaque handle to the pseudo console created by CreatePseudoConsole.
	 */
	void *hpc_ = nullptr;

	/**
	 * @brief Windows-specific: Pipe to ConPTY (input).
	 *
	 * Handle to the write end of the pipe that feeds data to the ConPTY.
	 */
	void *pipeIn_ = nullptr;

	/**
	 * @brief Windows-specific: Pipe from ConPTY (output).
	 *
	 * Handle to the read end of the pipe that receives data from the ConPTY.
	 */
	void *pipeOut_ = nullptr;

	/**
	 * @brief Windows-specific: Handle to the shell process.
	 *
	 * Process handle used to check if the shell is still running and
	 * to terminate it during close().
	 */
	void *procHandle_ = nullptr;
#else
	/**
	 * @brief Linux-specific: Master file descriptor for the PTY.
	 *
	 * File descriptor returned by forkpty() for communicating with the
	 * slave side of the pseudo-terminal.
	 */
	int masterFd_ = -1;

	/**
	 * @brief Linux-specific: PID of the shell process.
	 *
	 * Process ID of the child process spawned by forkpty(). Used to
	 * check if the shell is still running via waitpid().
	 */
	int childPid_ = -1;
#endif
};
