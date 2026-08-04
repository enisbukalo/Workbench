#include <gtest/gtest.h>
#include "core/eventBus.h"
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <string>

// EventBus is in global namespace (no namespace wrapper)

// =============================================================================
// EventBus Basic Tests
// =============================================================================

TEST(EventBus, SubscribeReturnsValidId) {
    // Each subscribe should return a unique, non-zero ID
    auto id1 = EventBus::subscribe("test.event1", [](const auto&, const auto*) {});
    auto id2 = EventBus::subscribe("test.event2", [](const auto&, const auto*) {});

    EXPECT_NE(id1, id2);
    EXPECT_NE(id1, 0u);
    EXPECT_NE(id2, 0u);

    // Cleanup
    EventBus::unsubscribe(id1);
    EventBus::unsubscribe(id2);
}

TEST(EventBus, UnsubscribeRemovesHandler) {
    std::atomic<int> callCount{0};

    auto handler = [&callCount](const auto&, const auto*) {
        callCount++;
    };

    auto id = EventBus::subscribe("test.event", handler);

    // Publish should trigger handler
    EventBus::publish("test.event", nullptr);
    EXPECT_EQ(callCount, 1);

    // Unsubscribe
    EventBus::unsubscribe(id);

    // Publish should NOT trigger handler
    EventBus::publish("test.event", nullptr);
    EXPECT_EQ(callCount, 1);  // Still 1, not 2
}

TEST(EventBus, UnsubscribeInvalidIdIsSafe) {
    // Should not crash or throw
    EXPECT_NO_THROW(EventBus::unsubscribe(0));
    EXPECT_NO_THROW(EventBus::unsubscribe(99999));
}

TEST(EventBus, PublishCallsHandlerWithCorrectData) {
    int receivedValue = 0;
    int testValue = 42;

    auto handler = [&receivedValue](const auto&, const void* data) {
        receivedValue = *static_cast<const int*>(data);
    };

    auto id = EventBus::subscribe("test.event", handler);
    EventBus::publish("test.event", &testValue);

    EXPECT_EQ(receivedValue, 42);

    EventBus::unsubscribe(id);
}

TEST(EventBus, MultipleSubscribersSameEvent) {
    std::atomic<int> count1{0};
    std::atomic<int> count2{0};

    auto handler1 = [&count1](const auto&, const auto*) { count1++; };
    auto handler2 = [&count2](const auto&, const auto*) { count2++; };

    auto id1 = EventBus::subscribe("test.event", handler1);
    auto id2 = EventBus::subscribe("test.event", handler2);

    EventBus::publish("test.event", nullptr);

    EXPECT_EQ(count1, 1);
    EXPECT_EQ(count2, 1);

    EventBus::unsubscribe(id1);
    EventBus::unsubscribe(id2);
}

TEST(EventBus, WildcardSubscriberReceivesAllEvents) {
    std::atomic<int> callCount{0};

    auto handler = [&callCount](const auto&, const auto*) {
        callCount++;
    };

    auto id = EventBus::subscribe("*", handler);

    EventBus::publish("event.one", nullptr);
    EventBus::publish("event.two", nullptr);
    EventBus::publish("event.three", nullptr);

    EXPECT_EQ(callCount, 3);

    EventBus::unsubscribe(id);
}

TEST(EventBus, SpecificSubscriberAlsoCalledWithWildcard) {
    std::atomic<int> specificCount{0};
    std::atomic<int> wildcardCount{0};

    auto specificHandler = [&specificCount](const auto&, const auto*) {
        specificCount++;
    };
    auto wildcardHandler = [&wildcardCount](const auto&, const auto*) {
        wildcardCount++;
    };

    auto id1 = EventBus::subscribe("test.event", specificHandler);
    auto id2 = EventBus::subscribe("*", wildcardHandler);

    EventBus::publish("test.event", nullptr);

    // Both should be called
    EXPECT_EQ(specificCount, 1);
    EXPECT_EQ(wildcardCount, 1);

    EventBus::unsubscribe(id1);
    EventBus::unsubscribe(id2);
}

// =============================================================================
// EventBus Data Tests
// =============================================================================

TEST(EventBus, PublishWithStringData) {
    std::string received;
    std::string testStr = "hello world";

    auto handler = [&received](const auto&, const void* data) {
        received = *static_cast<const std::string*>(data);
    };

    auto id = EventBus::subscribe("test.string", handler);
    EventBus::publish("test.string", &testStr);

    EXPECT_EQ(received, "hello world");

    EventBus::unsubscribe(id);
}

TEST(EventBus, PublishWithIntData) {
    int value = 123;
    int received = 0;

    auto handler = [&received](const auto&, const void* data) {
        received = *static_cast<const int*>(data);
    };

    auto id = EventBus::subscribe("test.int", handler);
    EventBus::publish("test.int", &value);

    EXPECT_EQ(received, 123);

    EventBus::unsubscribe(id);
}

TEST(EventBus, PublishWithStructData) {
    struct TestStruct {
        int a;
        double b;
        std::string c;
    };

    TestStruct value{1, 2.5, "test"};
    TestStruct received{0, 0.0, ""};

    auto handler = [&received](const auto&, const void* data) {
        received = *static_cast<const TestStruct*>(data);
    };

    auto id = EventBus::subscribe("test.struct", handler);
    EventBus::publish("test.struct", &value);

    EXPECT_EQ(received.a, 1);
    EXPECT_FLOAT_EQ(received.b, 2.5);
    EXPECT_EQ(received.c, "test");

    EventBus::unsubscribe(id);
}

// =============================================================================
// EventBus Thread Safety Tests
// =============================================================================

TEST(EventBus, ConcurrentSubscribeUnsubscribe) {
    std::atomic<bool> running{true};
    std::vector<std::thread> threads;

    // Start threads that concurrently subscribe/unsubscribe
    for (int i = 0; i < 4; i++) {
        threads.emplace_back([&running]() {
            while (running) {
                auto id = EventBus::subscribe("test.event", [](const auto&, const auto*) {});
                EventBus::unsubscribe(id);
            }
        });
    }

    // Let them run for a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    running = false;

    for (auto& t : threads) {
        t.join();
    }

    // Should complete without deadlock or crash
    EXPECT_TRUE(true);
}

TEST(EventBus, ConcurrentPublish) {
    std::atomic<int> publishCount{0};
    std::atomic<int> handlerCount{0};

    auto handler = [&handlerCount](const auto&, const auto*) {
        handlerCount++;
    };

    auto id = EventBus::subscribe("test.concurrent", handler);

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; i++) {
        threads.emplace_back([&publishCount]() {
            for (int j = 0; j < 100; j++) {
                EventBus::publish("test.concurrent", nullptr);
                publishCount++;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // All events should be delivered
    EXPECT_EQ(publishCount, 400);
    EXPECT_EQ(handlerCount, 400);

    EventBus::unsubscribe(id);
}

// =============================================================================
// EventBus Edge Cases
// =============================================================================

TEST(EventBus, EmptyEventId) {
    std::atomic<bool> called{false};

    auto handler = [&called](const auto&, const auto*) {
        called = true;
    };

    auto id = EventBus::subscribe("", handler);
    EventBus::publish("", nullptr);

    EXPECT_TRUE(called);

    EventBus::unsubscribe(id);
}

TEST(EventBus, HierarchicalEventNaming) {
    // Note: EventBus does NOT support hierarchical wildcard matching (e.g., "config.*").
    // Only the literal wildcard "*" is supported.
    // This test documents that behavior.
    
    std::atomic<int> wildcardCount{0};

    auto wildcardHandler = [&wildcardCount](const auto& event, const auto*) {
        wildcardCount++;
    };

    auto id = EventBus::subscribe("*", wildcardHandler);

    EventBus::publish("config.ui.theme", nullptr);
    EventBus::publish("config.server.host", nullptr);
    EventBus::publish("network.model.loaded", nullptr);

    // Wildcard "*" receives all events
    EXPECT_EQ(wildcardCount, 3);

    EventBus::unsubscribe(id);
}

TEST(EventBus, PublishNoSubscribers) {
    // Should not crash
    EXPECT_NO_THROW(EventBus::publish("nonexistent.event", nullptr));
}

TEST(EventBus, DoubleUnsubscribeIsSafe) {
    auto id = EventBus::subscribe("test.event", [](const auto&, const auto*) {});
    
    EventBus::unsubscribe(id);
    EXPECT_NO_THROW(EventBus::unsubscribe(id));
    EXPECT_NO_THROW(EventBus::unsubscribe(id));
}
