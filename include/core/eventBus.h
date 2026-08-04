#pragma once
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <cstdint>

/**
 * @file eventBus.h
 * @brief Generic publish-subscribe event bus for inter-component communication.
 *
 * EventBus provides a lightweight, thread-safe pub/sub mechanism for
 * decoupling components in the application. It supports:
 * - String-based event identifiers with hierarchical naming
 * - Wildcard subscriptions ("*") to receive all events
 * - Thread-safe subscription management
 * - Zero-copy event delivery via void* data pointer
 *
 * Design principles:
 * - Global pub/sub registry — no need to pass EventBus around
 * - String-based event IDs — e.g., "config.ui.refreshRateMs", "network.model.loaded"
 * - Wildcard support — "*" to subscribe to all events
 * - Thread-safe — uses mutex for subscription management
 * - Callback signature — (eventId, data) — data is void* for flexibility
 * - No inheritance — EventBus is a utility, not a base class
 *
 * Event naming convention:
 * @code
 * // Config events (hierarchical naming)
 * EventBus::publish("config.saved", &config);
 * EventBus::publish("config.ui.refreshRateMs", &newRate);
 * EventBus::publish("config.server.host", &newHost);
 *
 * // Network events
 * EventBus::publish("network.model.loaded", &modelInfo);
 *
 * // Terminal events
 * EventBus::publish("terminal.process.exited", &exitCode);
 * @endcode
 *
 * Usage example:
 * @code
 * // Subscribe to a specific event
 * auto subId = EventBus::subscribe("config.ui.refreshRateMs", 
 *     [](const std::string& event, const void* data) {
 *         int newRate = *static_cast<const int*>(data);
 *         std::cout << "Refresh rate changed to: " << newRate << "ms" << std::endl;
 *     });
 *
 * // Subscribe to all events
 * auto allSubId = EventBus::subscribe("*", 
 *     [](const std::string& event, const void* data) {
 *         std::cout << "Event fired: " << event << std::endl;
 *     });
 *
 * // Publish an event
 * int newRate = 500;
 * EventBus::publish("config.ui.refreshRateMs", &newRate);
 *
 * // Unsubscribe when done
 * EventBus::unsubscribe(subId);
 * @endcode
 *
 * Thread safety:
 * - subscribe(): Thread-safe, uses mutex
 * - unsubscribe(): Thread-safe, uses mutex
 * - publish(): Thread-safe for subscription iteration; caller responsible for handler thread safety
 * - Handlers are called synchronously from publish() thread
 *
 * @note EventBus is a utility class — do not inherit from it.
 * @note Event handlers are called synchronously; blocking handlers block publish().
 * @note The void* data pointer is not owned by EventBus; ensure data lifetime exceeds handler execution.
 */
class EventBus {
public:
    /**
     * @brief Event identifier type.
     *
     * Uses hierarchical naming convention for organization:
     * - "config.ui.refreshRateMs" — Config UI refresh rate changed
     * - "network.model.loaded" — Model loaded on server
     * - "terminal.process.exited" — Terminal process exited
     *
     * Special value "*" matches all events (wildcard).
     */
    using EventId = std::string;

    /**
     * @brief Event handler callback type.
     *
     * @param event The event identifier that was published
     * @param data Pointer to event data (type depends on event)
     *
     * @note Handlers are called synchronously from the publish() thread.
     * @note The data pointer is not owned by the handler; do not delete it.
     * @note For config events, data typically points to the changed value.
     */
    using Handler = std::function<void(const EventId& event, const void* data)>;

    /**
     * @brief Unique subscription identifier.
     *
     * Returned by subscribe() and used with unsubscribe() to remove
     * a subscription. Each subscription gets a unique ID.
     */
    using SubscriptionId = std::uint64_t;

    /**
     * @brief Subscribe to an event.
     *
     * Registers a handler to be called when the specified event is published.
     * Supports wildcard "*" to subscribe to all events.
     *
     * @param event Event identifier to subscribe to, or "*" for all events
     * @param handler Callback function to invoke when event is published
     * @return Unique subscription ID for later unsubscribe()
     *
     * @note Thread-safe; can be called from any thread.
     * @note Wildcard "*" subscribers receive all events including named events.
     * @note Handler is stored by value; ensure it doesn't capture resources
     *       with shorter lifetime than the subscription.
     *
     * @code
     * // Subscribe to specific event
     * auto id1 = EventBus::subscribe("config.ui.refreshRateMs", handler);
     *
     * // Subscribe to all events
     * auto id2 = EventBus::subscribe("*", wildcardHandler);
     * @endcode
     */
    static SubscriptionId subscribe(const EventId& event, Handler handler);

    /**
     * @brief Unsubscribe from an event.
     *
     * Removes the subscription with the given ID. Safe to call multiple
     * times with the same ID (subsequent calls are no-ops).
     *
     * @param id Subscription ID returned by subscribe()
     *
     * @note Thread-safe; can be called from any thread.
     * @note Calling with an invalid or already-unsubscribed ID is safe (no-op).
     * @note Handlers should not unsubscribe from themselves; deadlock possible.
     *
     * @code
     * EventBus::unsubscribe(subscriptionId);
     * @endcode
     */
    static void unsubscribe(SubscriptionId id);

    /**
     * @brief Publish an event to all matching subscribers.
     *
     * Invokes all handlers subscribed to the event ID, plus all wildcard
     * ("*") subscribers. Handlers are called synchronously in no particular order.
     *
     * @param event Event identifier to publish
     * @param data Pointer to event data (owned by caller)
     *
     * @note Thread-safe; can be called from any thread.
     * @note Handlers are called synchronously; blocking handlers block this call.
     * @note The data pointer must remain valid for the duration of this call.
     * @note No exception safety guarantees for handlers; exceptions propagate.
     *
     * @code
     * int newRate = 500;
     * EventBus::publish("config.ui.refreshRateMs", &newRate);
     * @endcode
     *
     * @throws std::exception If a handler throws, it propagates to caller.
     */
    static void publish(const EventId& event, const void* data);

private:
    /**
     * @brief Internal state holder — singleton data.
     *
     * Contains all subscription state. Accessed via getInstance().
     *
     * @note This struct encapsulates all mutable state for easy reasoning.
     * @note Using a struct instead of private members keeps the interface clean.
     */
    struct State {
        /** Map from event ID to list of (handler, subscription_id) pairs */
        std::map<EventId, std::vector<std::pair<Handler, SubscriptionId>>> subscriptions;
        
        /** Mutex protecting subscriptions map */
        std::mutex mutex;
        
        /** Counter for generating unique subscription IDs */
        SubscriptionId nextSubscriptionId = 1;
    };

    /**
     * @brief Get the singleton state instance.
     *
     * @return Reference to the singleton State instance.
     *
     * @note Uses Meyers' singleton pattern — thread-safe in C++11 and later.
     * @note State is constructed on first access, destroyed at program exit.
     */
    static State& getInstance();
};
