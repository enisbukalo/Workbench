#include "eventBus.h"

#include <algorithm>
#include <spdlog/spdlog.h>

/**
 * @file eventBus.cpp
 * @brief Implementation of generic publish-subscribe event bus.
 *
 * This file implements the EventBus class — a lightweight, thread-safe
 * pub/sub mechanism for inter-component communication.
 */

/**
 * @brief Get the singleton state instance.
 *
 * Uses Meyers' singleton pattern — thread-safe in C++11 and later.
 * The State object is constructed on first access and destroyed
 * at program exit (static storage duration).
 *
 * @return Reference to the singleton State instance.
 *
 * @note Thread-safe by virtue of C++11 guarantee for function-local
 *       static initialization.
 * @note No dynamic allocation overhead after first access.
 * @note Destruction order is guaranteed at program termination.
 */
EventBus::State &EventBus::getInstance()
{
	static State instance;
	return instance;
}

/**
 * @brief Subscribe to an event.
 *
 * Registers a handler to be called when the specified event is published.
 * Supports wildcard "*" to subscribe to all events.
 *
 * Thread-safety: Uses mutex to protect subscriptions map during insertion.
 * The lock is held for the duration of the insertion — typically very brief.
 *
 * @param event Event identifier to subscribe to, or "*" for all events
 * @param handler Callback function to invoke when event is published
 * @return Unique subscription ID for later unsubscribe()
 *
 * @note The subscription ID is a monotonically increasing counter.
 * @note Wildcard "*" subscribers receive all events including named events.
 * @note Handler is stored by value via std::function.
 *
 * Complexity: O(log N + M) where N = number of unique event IDs,
 *            M = number of existing subscribers to this event
 */
EventBus::SubscriptionId EventBus::subscribe(const EventId &event,
											 Handler handler)
{
	State &state = getInstance();

	std::lock_guard<std::mutex> lock(state.mutex);

	// Generate unique subscription ID
	SubscriptionId id = state.nextSubscriptionId++;

	// Insert handler into the appropriate event's subscriber list
	// std::map::operator[] creates entry with default-constructed vector if not
	// exists
	state.subscriptions[event].emplace_back(std::move(handler), id);

	spdlog::debug("Subscribed to '{}' (id: {})", event, id);

	return id;
}

/**
 * @brief Unsubscribe from an event.
 *
 * Removes the subscription with the given ID. Searches all event buckets
 * to find the subscription — this is intentional to support wildcard
 * subscriptions and flexible event routing.
 *
 * Thread-safety: Uses mutex to protect subscriptions map during modification.
 *
 * @param id Subscription ID returned by subscribe()
 *
 * @note Safe to call multiple times with same ID (subsequent calls are no-ops).
 * @note O(N * M) where N = number of unique event IDs, M = subscribers per
 * event. This is acceptable since unsubscribe is typically infrequent.
 * @note Handlers should not unsubscribe from themselves — deadlock possible.
 *
 * Complexity: O(N * M) where N = number of unique event IDs,
 *            M = average number of subscribers per event
 */
void EventBus::unsubscribe(SubscriptionId id)
{
	State &state = getInstance();

	std::lock_guard<std::mutex> lock(state.mutex);

	// Search through all event buckets to find the subscription
	for (auto &[eventId, subscribers] : state.subscriptions) {
		// Find the subscriber with matching ID
		auto it =
			std::find_if(subscribers.begin(),
						 subscribers.end(),
						 [id](const auto &pair) { return pair.second == id; });

		// Remove if found
		if (it != subscribers.end()) {
			subscribers.erase(it);

			// Clean up empty event buckets to prevent memory leak
			if (subscribers.empty()) {
				state.subscriptions.erase(eventId);
			}

			spdlog::debug("Unsubscribed (id: {})", id);

			// Subscription ID is unique — stop after first removal
			return;
		}
	}

	// ID not found — safe to ignore (idempotent operation)
	spdlog::debug("Unsubscribe: id {} not found (already removed)", id);
}

/**
 * @brief Publish an event to all matching subscribers.
 *
 * Invokes all handlers subscribed to the event ID, plus all wildcard
 * ("*") subscribers. Handlers are called synchronously in no particular order.
 *
 * Thread-safety: Reads subscriptions under mutex lock, then calls handlers
 * outside the lock to prevent deadlock if handlers try to subscribe/unsubscribe.
 * Note: This means a handler could be called after unsubscribe if another
 * thread unsubscribes during publish — acceptable trade-off for performance.
 *
 * @param event Event identifier to publish
 * @param data Pointer to event data (owned by caller)
 *
 * @note Handlers are called synchronously; blocking handlers block this call.
 * @note The data pointer must remain valid for the duration of this call.
 * @note Wildcard "*" subscribers are always notified, regardless of event.
 * @note Exceptions from handlers propagate to the caller.
 *
 * Complexity: O(S) where S = number of subscribers (named + wildcard)
 */
void EventBus::publish(const EventId &event, const void *data)
{
	State &state = getInstance();

	// Copy handlers to a local vector while holding the lock
	// This prevents handlers from being modified during iteration
	// and allows handlers to safely subscribe/unsubscribe during execution
	std::vector<Handler> handlersToCall;

	{
		std::lock_guard<std::mutex> lock(state.mutex);

		// Add named event subscribers. Handlers are copied (not moved) because
		// the subscription must survive this publish — subscribers remain
		// registered for future events.
		auto it = state.subscriptions.find(event);
		if (it != state.subscriptions.end()) {
			for (const auto &[handler, _] : it->second) {
				handlersToCall.push_back(handler);
			}
		}

		// Add wildcard subscribers
		auto wildcardIt = state.subscriptions.find("*");
		if (wildcardIt != state.subscriptions.end()) {
			for (const auto &[handler, _] : wildcardIt->second) {
				handlersToCall.push_back(handler);
			}
		}
	} // Lock released here

	// Call all handlers outside the lock
	// This prevents deadlock if a handler tries to subscribe/unsubscribe
	if (handlersToCall.empty()) {
		spdlog::debug("Published '{}' - no subscribers", event);
	} else {
		spdlog::debug("Published '{}' to {} subscriber(s)",
					  event,
					  handlersToCall.size());
	}

	for (auto &handler : handlersToCall) {
		handler(event, data);
	}
}
