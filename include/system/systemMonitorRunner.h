#pragma once
#include "eventBus.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace ftxui {
// v7 renamed ScreenInteractive -> App; ScreenInteractive is now an alias, so the
// forward declaration must name the real class.
class App;
using ScreenInteractive = App;
} // namespace ftxui

/**
 * @file systemMonitorRunner.h
 * @brief Singleton that manages a background thread polling all monitors.
 *
 * This class implements a background monitoring thread that:
 * 1. Polls all hardware monitors (CPU, GPU, RAM) at configurable intervals
 * 2. Dynamically updates polling interval via EventBus subscription
 * 3. Provides clean shutdown via stop() or destructor
 *
 * Thread model:
 * - Background thread: Calls update() on all monitors
 * - Main thread: FTXUI's Screen::Loop() handles all rendering independently
 * - Shared state: Protected by condition variable and mutex for coordination
 *
 * Timing:
 * - Polling interval: Configurable via refreshRateMs_ (default 250ms)
 * - Effective update rate: Dynamic, updated via "config.ui.refreshRateMs" event
 *
 * Singleton lifecycle:
 * - Created: First call to instance()
 * - Started: start(refreshRateMs) called once from main()
 * - Stopped: stop() called explicitly or via destructor
 *
 * @note This class follows Meyers' singleton pattern — thread-safe, lazy
 *       initialization, automatic cleanup at program termination.
 * @note start() can only be called once; subsequent calls are ignored.
 * @note This class does NOT trigger UI redraws; FTXUI's Screen::Loop() handles
 *       all rendering independently. The background thread only updates monitor
 * data.
 */
class SystemMonitorRunner
{
  public:
	/**
	 * @brief Get the singleton instance.
	 *
	 * Creates the instance on first call using Meyers' singleton pattern.
	 * Thread-safe in C++11 and later.
	 *
	 * @return Reference to the singleton SystemMonitorRunner instance.
	 *
	 * @code
	 * // In main.cpp:
	 * SystemMonitorRunner::instance().start(screen, config.ui.refreshRateMs);
	 * @endcode
	 *
	 * @note The instance is destroyed at program termination.
	 * @note This method is thread-safe; multiple simultaneous calls return
	 *       the same instance.
	 */
	static SystemMonitorRunner &instance();

	/**
	 * @brief Start the background polling thread.
	 *
	 * Initializes the singleton with the initial refresh rate, subscribes
	 * to "config.ui.refreshRateMs" events for dynamic updates, and starts
	 * the background polling thread.
	 *
	 * This method:
	 * 1. Sets the initial refresh rate
	 * 2. Subscribes to refresh rate change events
	 * 3. Starts the background polling thread
	 *
	 * @param refreshRateMs Initial polling interval in milliseconds.
	 *
	 * @note Can only be called once; subsequent calls are ignored.
	 * @note The background thread starts polling immediately.
	 * @note The screen pointer is stored and used to call Repaint() after each
	 * monitor update, forcing the UI to refresh with new data.
	 * @note The screen must outlive the SystemMonitorRunner.
	 *
	 * @code
	 * // In app.cpp (after screen creation):
	 * auto screen = App::Fullscreen();
	 * SystemMonitorRunner::instance().start(&screen, config.ui.refreshRateMs);
	 * @endcode
	 */
	void start(ftxui::ScreenInteractive *screen, int refreshRateMs);

	/**
	 * @brief Stop the background polling thread.
	 *
	 * This method:
	 * 1. Unsubscribes from EventBus
	 * 2. Sets stopFlag_ to true
	 * 3. Notifies cv_ to wake the polling thread
	 * 4. Joins the background thread
	 *
	 * @note Safe to call multiple times; subsequent calls are no-ops.
	 * @note Blocks until the background thread has joined.
	 * @note Should be called from main() before exit for clean shutdown.
	 *
	 * @code
	 * // In main.cpp:
	 * App::run();
	 * SystemMonitorRunner::instance().stop();
	 * return 0;
	 * @endcode
	 */
	void stop();

	/**
	 * @brief Deleted copy constructor — not copyable by design.
	 *
	 * Singleton cannot be copied.
	 */
	SystemMonitorRunner(const SystemMonitorRunner &) = delete;

	/**
	 * @brief Deleted copy-assignment operator — not copyable by design.
	 *
	 * Singleton cannot be copied.
	 */
	SystemMonitorRunner &operator=(const SystemMonitorRunner &) = delete;

  private:
	/**
	 * @brief Private constructor for singleton.
	 *
	 * Initializes the singleton instance. Called only by instance().
	 */
	SystemMonitorRunner();

	/**
	 * @brief Destructor. Calls @ref stop() to cleanly join the background
	 * thread.
	 *
	 * This destructor:
	 * 1. Calls stop() to terminate the background thread
	 * 2. Unsubscribes from EventBus (if still subscribed)
	 * 3. Ensures clean shutdown
	 *
	 * @note Safe to call multiple times; subsequent calls are no-ops.
	 * @note The destructor blocks until the background thread has joined.
	 */
	~SystemMonitorRunner();

	/**
	 * @brief Event handler for EventBus.
	 *
	 * Called when a subscribed event is published. Currently handles:
	 * - "config.ui.refreshRateMs": Updates the polling interval dynamically
	 *
	 * @param event Event identifier
	 * @param data Pointer to event data (e.g., new refresh rate value)
	 *
	 * @note Called from the thread that publishes the event (typically UI
	 * thread).
	 * @note Uses atomic store for lock-free refresh rate updates.
	 *
	 * @code
	 * // In SettingsPanel::saveConfig():
	 * if (oldRefreshRate != newRefreshRate) {
	 *     EventBus::publish("config.ui.refreshRateMs", &newRefreshRate);
	 * }
	 * @endcode
	 */
	void onEvent(const EventBus::EventId &event, const void *data);

	/**
	 * @brief Background thread function that polls monitors.
	 *
	 * This loop:
	 * 1. Calls update() on CpuMonitor, MemoryMonitor, and GpuMonitor
	 * 2. Triggers a UI redraw via screen_->Repaint()
	 * 3. Waits for refreshRateMs_ or until stopFlag_ is set
	 * 4. Repeats until stopFlag_ is true
	 *
	 * The loop uses a condition variable to allow the thread to wake up
	 * immediately if stop() is called, rather than waiting for the full
	 * polling interval.
	 *
	 * @note This method is called by the thread_ member.
	 * @note All monitor update() calls are thread-safe.
	 * @note Uses dynamic refreshRateMs_ instead of constant kThreadWaitTimeMs.
	 * @note Calls screen_->Repaint() after each update to force UI refresh.
	 *       FTXUI's Repaint() is thread-safe and schedules a redraw on the
	 *       main thread's next iteration of Screen::Loop().
	 */
	void run();

	/**
	 * @brief Dynamic polling interval in milliseconds.
	 *
	 * The background thread waits this duration between poll cycles.
	 * Updated dynamically via EventBus subscription to
	 * "config.ui.refreshRateMs" events.
	 *
	 * @note Uses std::atomic for lock-free reads/writes.
	 * @note Default value: 250ms (4 Hz update rate)
	 * @note Set by start() and updated by onEvent().
	 */
	std::atomic<int> refreshRateMs_{ 250 };

	/**
	 * @brief Stop flag for the background thread.
	 *
	 * When set to true, the run() loop terminates and joins.
	 * Protected by atomic operations for thread-safe access.
	 */
	std::atomic<bool> stopFlag_{ false };

	/**
	 * @brief Mutex for condition variable access.
	 *
	 * Protects access to cv_ during wait/notify operations.
	 */
	std::mutex cvMutex_;

	/**
	 * @brief Condition variable for thread coordination.
	 *
	 * Used to wake the polling thread when stop() is called, allowing
	 * immediate shutdown rather than waiting for the full polling interval.
	 */
	std::condition_variable cv_;

	/**
	 * @brief Background thread for polling monitors.
	 *
	 * Declared last to ensure proper initialization order: stopFlag_,
	 * cvMutex_, and cv_ must be initialized before thread_ starts.
	 */
	std::thread thread_;

	/**
	 * @brief Ensures start() can only be called once.
	 *
	 * Used with std::call_once to guarantee the background thread
	 * is started exactly once, even if start() is called multiple times.
	 *
	 * @note This provides thread-safe one-time initialization.
	 */
	std::once_flag startFlag_;

	/**
	 * @brief EventBus subscription ID for refresh rate changes.
	 *
	 * Used to unsubscribe when stop() is called or in destructor.
	 * Subscribes to "config.ui.refreshRateMs" events.
	 *
	 * @note Initialized to 0 (invalid subscription).
	 * @note Set by start(), cleared by stop().
	 */
	EventBus::SubscriptionId subscriptionId_ = 0;

	/**
	 * @brief Pointer to the FTXUI screen for triggering redraws.
	 *
	 * This pointer is used to call Repaint() after updating monitor data,
	 * forcing FTXUI to re-render the UI with fresh data.
	 *
	 * @note Set by start(screen, refreshRateMs).
	 * @note The screen must outlive the SystemMonitorRunner.
	 * @note FTXUI's Repaint() is thread-safe and can be called from the
	 *       background thread.
	 */
	ftxui::ScreenInteractive *screen_ = nullptr;
};
