#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "mqtt/MqttClient.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <ctime>
#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <vector>
#include <unistd.h>
#include "test_mqtt_env.h"

using namespace hms_nut;
using namespace std::chrono_literals;

namespace {

// A topic no other run shares, so retained messages and earlier runs never
// cross into a test.
std::string uniqueTopic(const std::string& leaf) {
    static const std::string run =
        std::to_string(::getpid()) + "_" + std::to_string(std::time(nullptr));
    return "hms_nut_test/" + run + "/" + leaf;
}

// Polls pred until it holds or the deadline passes.
template <typename Pred>
bool waitUntil(Pred pred, std::chrono::milliseconds deadline) {
    auto until = std::chrono::steady_clock::now() + deadline;
    while (!pred()) {
        if (std::chrono::steady_clock::now() >= until) return false;
        std::this_thread::sleep_for(10ms);
    }
    return true;
}

// Runs fn on its own thread and reports whether it returned within the
// deadline, so a deadlock fails the test instead of freezing the run. On a
// timeout the thread stays blocked, detached: fn must capture only what it
// owns (shared_ptrs, copies) and the caller must leakWedged() the client.
template <typename Fn>
bool finishesWithin(Fn fn, std::chrono::seconds deadline) {
    auto done = std::make_shared<std::promise<void>>();
    auto fut = done->get_future();
    std::thread([done, fn]() mutable {
        fn();
        done->set_value();
    }).detach();
    return fut.wait_for(deadline) == std::future_status::ready;
}

// A deadlocked client cannot be destroyed: its destructor disconnects, which
// blocks on the same deadlock. A failed test leaks it on purpose and lets the
// process exit around it.
void leakWedged(std::shared_ptr<MqttClient> client) {
    new std::shared_ptr<MqttClient>(std::move(client));
}

constexpr int kSubscriberThreads = 8;
constexpr int kTopicsPerThread = 10;

}  // namespace

/**
 * Unit tests for async MQTT subscription behavior
 *
 * These tests verify that the HTTP server blocking issue is fixed by ensuring:
 * 1. Subscriptions don't block the calling thread
 * 2. Callbacks are registered immediately
 * 3. Multiple concurrent subscriptions work
 * 4. Retained messages don't cause blocking
 */
class AsyncSubscriptionTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Use unique client ID for each test
        client_id_ = "test_async_" + std::to_string(std::time(nullptr));
    }

    void TearDown() override {
        // Clean up if needed
    }

    std::string client_id_;
};

/**
 * Test 1: Verify subscription returns immediately (non-blocking)
 *
 * This is the CRITICAL test that ensures the HTTP server blocking bug is fixed.
 * Before fix: subscribe() would block for 5+ seconds waiting for SUBACK
 * After fix: subscribe() returns within milliseconds
 */
TEST_F(AsyncSubscriptionTest, SubscriptionReturnsImmediately) {
    SKIP_WITHOUT_TEST_BROKER();
    auto mqtt_client = std::make_shared<MqttClient>(client_id_);

    // Connect to broker (using environment variables or defaults)
    std::string url  = mqtt_test_url();
    std::string user = mqtt_test_user();
    std::string pass = mqtt_test_password();

    ASSERT_TRUE(mqtt_client->connect(url, user, pass)) << "Failed to connect to MQTT broker";

    // Measure time taken to subscribe
    auto start = std::chrono::high_resolution_clock::now();

    bool callback_called = false;
    bool result = mqtt_client->subscribe("test/async/topic1",
        [&callback_called](const std::string& topic, const std::string& payload) {
            callback_called = true;
        }, 1);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    // Assert subscription returned quickly (< 100ms = non-blocking)
    EXPECT_TRUE(result) << "Subscription should succeed";
    EXPECT_LT(duration_ms, 100) << "Subscription should return in < 100ms (was " << duration_ms << "ms)";

    mqtt_client->disconnect();
}

/**
 * Test 2: Verify multiple subscriptions don't accumulate blocking time
 *
 * Before fix: 3 subscriptions would block for 15+ seconds (5s each)
 * After fix: 3 subscriptions complete in < 300ms total
 */
TEST_F(AsyncSubscriptionTest, MultipleSubscriptionsNonBlocking) {
    SKIP_WITHOUT_TEST_BROKER();
    auto mqtt_client = std::make_shared<MqttClient>(client_id_ + "_multi");

    std::string url  = mqtt_test_url();
    std::string user = mqtt_test_user();
    std::string pass = mqtt_test_password();

    ASSERT_TRUE(mqtt_client->connect(url, user, pass));

    auto start = std::chrono::high_resolution_clock::now();

    // Subscribe to 3 topics (simulating homeassistant/status + sensor topics)
    std::atomic<int> callback_count{0};

    bool r1 = mqtt_client->subscribe("test/async/topic1",
        [&callback_count](const std::string& topic, const std::string& payload) {
            callback_count++;
        }, 1);

    bool r2 = mqtt_client->subscribe("test/async/topic2",
        [&callback_count](const std::string& topic, const std::string& payload) {
            callback_count++;
        }, 1);

    bool r3 = mqtt_client->subscribe("test/async/topic3",
        [&callback_count](const std::string& topic, const std::string& payload) {
            callback_count++;
        }, 1);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    EXPECT_TRUE(r1 && r2 && r3) << "All subscriptions should succeed";
    EXPECT_LT(duration_ms, 300) << "3 subscriptions should complete in < 300ms (was " << duration_ms << "ms)";

    mqtt_client->disconnect();
}

/**
 * Test 3: Verify subscription works even with retained messages
 *
 * This simulates the homeassistant/status retained "online" message scenario.
 * Before fix: Retained message during subscription would cause blocking/timeout
 * After fix: Retained message handled correctly without blocking
 */
TEST_F(AsyncSubscriptionTest, SubscriptionHandlesRetainedMessages) {
    SKIP_WITHOUT_TEST_BROKER();
    auto mqtt_client = std::make_shared<MqttClient>(client_id_ + "_retained");

    std::string url  = mqtt_test_url();
    std::string user = mqtt_test_user();
    std::string pass = mqtt_test_password();

    ASSERT_TRUE(mqtt_client->connect(url, user, pass));

    // First, publish a retained message
    std::string test_topic = "test/async/retained";
    mqtt_client->publish(test_topic, "retained_payload", 1, true);  // retain=true

    // Small delay to ensure message is retained
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Now subscribe (will receive retained message immediately)
    auto start = std::chrono::high_resolution_clock::now();

    std::atomic<bool> callback_called{false};
    std::string received_payload;

    bool result = mqtt_client->subscribe(test_topic,
        [&callback_called, &received_payload](const std::string& topic, const std::string& payload) {
            callback_called = true;
            received_payload = payload;
        }, 1);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    // Subscription should return immediately even though retained message will arrive
    EXPECT_TRUE(result) << "Subscription should succeed";
    EXPECT_LT(duration_ms, 100) << "Subscription should return quickly (was " << duration_ms << "ms)";

    // Wait a bit for retained message to arrive
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Callback should have been called with retained message
    EXPECT_TRUE(callback_called) << "Callback should receive retained message";
    EXPECT_EQ(received_payload, "retained_payload") << "Should receive correct retained payload";

    mqtt_client->disconnect();
}

/**
 * Test 4: Verify callback is registered BEFORE subscription completes
 *
 * This ensures that even if SUBACK arrives very quickly, the callback is already in place
 */
TEST_F(AsyncSubscriptionTest, CallbackRegisteredBeforeSubscription) {
    SKIP_WITHOUT_TEST_BROKER();
    auto mqtt_client = std::make_shared<MqttClient>(client_id_ + "_callback");

    std::string url  = mqtt_test_url();
    std::string user = mqtt_test_user();
    std::string pass = mqtt_test_password();

    ASSERT_TRUE(mqtt_client->connect(url, user, pass));

    std::string test_topic = "test/async/callback_order";
    std::atomic<bool> callback_called{false};

    // Subscribe (callback should be registered immediately)
    bool result = mqtt_client->subscribe(test_topic,
        [&callback_called](const std::string& topic, const std::string& payload) {
            callback_called = true;
        }, 1);

    EXPECT_TRUE(result) << "Subscription should succeed";

    // Immediately publish to same topic
    mqtt_client->publish(test_topic, "test_payload", 1, false);

    // Wait for message delivery
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Callback should have been called (proves callback was registered before SUBACK)
    EXPECT_TRUE(callback_called) << "Callback should be called even when publish happens immediately";

    mqtt_client->disconnect();
}

/**
 * Test 5: Concurrent subscriptions from multiple threads
 *
 * 8 threads subscribe to 10 topics each, all at once, while another thread
 * publishes nonstop, as the NUT poll loop does in the service. subscribe()
 * must never give up because the client is busy: every one of the 80 calls
 * must return true, and every one of the 80 topics must then really deliver.
 */
TEST_F(AsyncSubscriptionTest, ConcurrentSubscriptionsThreadSafe) {
    SKIP_WITHOUT_TEST_BROKER();
    auto mqtt_client = std::make_shared<MqttClient>(client_id_ + "_concurrent");
    ASSERT_TRUE(mqtt_client->connect(mqtt_test_url(), mqtt_test_user(), mqtt_test_password()));
    MqttClient* raw = mqtt_client.get();

    struct State {
        std::mutex m;
        std::set<std::string> received;
        std::atomic<int> succeeded{0};
        std::atomic<int> ready{0};
        std::atomic<bool> stop{false};
    };
    auto st = std::make_shared<State>();
    const std::string base = uniqueTopic("concurrent");

    auto topicFor = [base](int t, int i) {
        return base + "/t" + std::to_string(t) + "/" + std::to_string(i);
    };

    bool returned = finishesWithin([raw, st, base, topicFor] {
        std::thread publisher([raw, st, base] {
            while (!st->stop) {
                raw->publish(base + "/noise/state", "x", 0, false);
                std::this_thread::sleep_for(200us);
            }
        });
        std::vector<std::thread> threads;
        for (int t = 0; t < kSubscriberThreads; ++t) {
            threads.emplace_back([raw, st, t, topicFor] {
                st->ready++;
                while (st->ready < kSubscriberThreads) std::this_thread::yield();
                for (int i = 0; i < kTopicsPerThread; ++i) {
                    bool ok = raw->subscribe(topicFor(t, i),
                        [st](const std::string& topic, const std::string&) {
                            std::lock_guard<std::mutex> lock(st->m);
                            st->received.insert(topic);
                        }, 1);
                    if (ok) st->succeeded++;
                }
            });
        }
        for (auto& th : threads) th.join();
        st->stop = true;
        publisher.join();
    }, 30s);

    if (!returned) {
        leakWedged(mqtt_client);
        FAIL() << "concurrent subscribe()/publish() did not finish within 30 s: deadlock";
    }

    const int total = kSubscriberThreads * kTopicsPerThread;
    EXPECT_EQ(st->succeeded.load(), total)
        << "subscribe() returned false for " << (total - st->succeeded.load())
        << " of " << total << " concurrent subscriptions";

    // Returning true is not enough: each topic must actually be subscribed.
    std::this_thread::sleep_for(500ms);
    for (int t = 0; t < kSubscriberThreads; ++t)
        for (int i = 0; i < kTopicsPerThread; ++i)
            ASSERT_TRUE(mqtt_client->publish(topicFor(t, i), "ping", 1, false));

    bool all = waitUntil([st, total] {
        std::lock_guard<std::mutex> lock(st->m);
        return static_cast<int>(st->received.size()) == total;
    }, 10s);
    std::size_t got;
    {
        std::lock_guard<std::mutex> lock(st->m);
        got = st->received.size();
    }
    EXPECT_TRUE(all) << "only " << got << " of " << total << " subscribed topics delivered";

    mqtt_client->disconnect();
}

/**
 * Regression: a message callback that subscribes.
 *
 * The client used to run callbacks while holding its callback table lock, a
 * plain std::mutex that subscribe() takes too, so a callback that subscribed
 * deadlocked Paho's receive thread on itself, and with it every delivery.
 */
TEST_F(AsyncSubscriptionTest, CallbackCanSubscribeWithoutDeadlock) {
    SKIP_WITHOUT_TEST_BROKER();
    auto mqtt_client = std::make_shared<MqttClient>(client_id_ + "_cb_subscribe");
    ASSERT_TRUE(mqtt_client->connect(mqtt_test_url(), mqtt_test_user(), mqtt_test_password()));
    MqttClient* raw = mqtt_client.get();

    struct State {
        std::atomic<bool> fired{false};
        std::atomic<bool> done{false};
        std::atomic<bool> inner_ok{false};
        std::atomic<int> second_hits{0};
    };
    auto st = std::make_shared<State>();
    const std::string trigger = uniqueTopic("cb_subscribe/trigger");
    const std::string second = uniqueTopic("cb_subscribe/second");

    ASSERT_TRUE(mqtt_client->subscribe(trigger,
        [raw, st, second](const std::string&, const std::string&) {
            if (st->fired.exchange(true)) return;
            st->inner_ok = raw->subscribe(second,
                [st](const std::string&, const std::string&) { st->second_hits++; }, 1);
            st->done = true;
        }, 1));
    std::this_thread::sleep_for(300ms);  // let the SUBACK land
    ASSERT_TRUE(mqtt_client->publish(trigger, "go", 1, false));

    if (!waitUntil([st] { return st->done.load(); }, 5s)) {
        leakWedged(mqtt_client);
        FAIL() << "subscribe() from inside a message callback did not return within 5 s: "
                  "the receive thread is deadlocked";
    }
    EXPECT_TRUE(st->inner_ok) << "subscribe() from a callback reported failure";

    std::this_thread::sleep_for(300ms);
    ASSERT_TRUE(mqtt_client->publish(second, "hello", 1, false));
    EXPECT_TRUE(waitUntil([st] { return st->second_hits.load() > 0; }, 5s))
        << "the subscription made inside a callback never delivered";

    mqtt_client->disconnect();
}

/**
 * Regression: unsubscribe() while a callback publishes.
 *
 * The service does exactly this: CollectorService::reloadSubscriptions()
 * unsubscribes a removed device while the homeassistant/status handler
 * republishes discovery on Paho's receive thread. unsubscribe() used to hold
 * the connection lock while it waited for the UNSUBACK; the callback's
 * publish() blocked on that lock, so the receive thread could never read the
 * UNSUBACK, and both threads hung for good.
 */
TEST_F(AsyncSubscriptionTest, UnsubscribeWhileCallbackPublishesDoesNotDeadlock) {
    SKIP_WITHOUT_TEST_BROKER();
    auto mqtt_client = std::make_shared<MqttClient>(client_id_ + "_unsub");
    ASSERT_TRUE(mqtt_client->connect(mqtt_test_url(), mqtt_test_user(), mqtt_test_password()));
    MqttClient* raw = mqtt_client.get();

    struct State {
        std::atomic<bool> entered{false};
        std::atomic<bool> finished{false};
        std::atomic<bool> published{false};
    };
    auto st = std::make_shared<State>();
    const std::string trigger = uniqueTopic("unsub/trigger");
    const std::string removed = uniqueTopic("unsub/removed");
    const std::string echo = uniqueTopic("unsub/echo");

    ASSERT_TRUE(mqtt_client->subscribe(removed,
        [](const std::string&, const std::string&) {}, 1));
    ASSERT_TRUE(mqtt_client->subscribe(trigger,
        [raw, st, echo](const std::string&, const std::string&) {
            if (st->entered.exchange(true)) return;
            // Still running when the other thread is inside unsubscribe().
            std::this_thread::sleep_for(500ms);
            st->published = raw->publish(echo, "republish", 1, false);
            st->finished = true;
        }, 1));
    std::this_thread::sleep_for(300ms);
    ASSERT_TRUE(mqtt_client->publish(trigger, "online", 1, false));
    ASSERT_TRUE(waitUntil([st] { return st->entered.load(); }, 5s))
        << "trigger message never arrived";

    auto unsub_ok = std::make_shared<std::atomic<bool>>(false);
    bool returned = finishesWithin([raw, removed, unsub_ok] {
        *unsub_ok = raw->unsubscribe(removed);
    }, 10s);
    if (!returned) {
        leakWedged(mqtt_client);
        FAIL() << "unsubscribe() did not return within 10 s while a callback published: deadlock";
    }
    EXPECT_TRUE(*unsub_ok);
    EXPECT_TRUE(waitUntil([st] { return st->finished.load(); }, 5s))
        << "the callback's publish() never returned";
    EXPECT_TRUE(st->published);

    mqtt_client->disconnect();
}

/**
 * Regression: disconnect() while a callback publishes.
 *
 * Same lock order as above, on shutdown: disconnect() held the connection
 * lock while it waited on Paho, and a callback (or the reconnect handler) on
 * Paho's thread waited for that lock. This is the hang seen in
 * test_ha_status_subscription's teardown.
 */
TEST_F(AsyncSubscriptionTest, DisconnectWhileCallbackPublishesDoesNotDeadlock) {
    SKIP_WITHOUT_TEST_BROKER();
    auto mqtt_client = std::make_shared<MqttClient>(client_id_ + "_disc");
    ASSERT_TRUE(mqtt_client->connect(mqtt_test_url(), mqtt_test_user(), mqtt_test_password()));
    MqttClient* raw = mqtt_client.get();

    struct State {
        std::atomic<bool> entered{false};
        std::atomic<bool> finished{false};
    };
    auto st = std::make_shared<State>();
    const std::string trigger = uniqueTopic("disc/trigger");
    const std::string echo = uniqueTopic("disc/echo");

    ASSERT_TRUE(mqtt_client->subscribe(trigger,
        [raw, st, echo](const std::string&, const std::string&) {
            if (st->entered.exchange(true)) return;
            std::this_thread::sleep_for(500ms);
            raw->publish(echo, "late", 1, false);  // may fail once disconnected
            st->finished = true;
        }, 1));
    std::this_thread::sleep_for(300ms);
    ASSERT_TRUE(mqtt_client->publish(trigger, "go", 1, false));
    ASSERT_TRUE(waitUntil([st] { return st->entered.load(); }, 5s))
        << "trigger message never arrived";

    bool returned = finishesWithin([raw] { raw->disconnect(); }, 10s);
    if (!returned) {
        leakWedged(mqtt_client);
        FAIL() << "disconnect() did not return within 10 s while a callback published: deadlock";
    }
    EXPECT_TRUE(waitUntil([st] { return st->finished.load(); }, 5s))
        << "the callback's publish() never returned";
    EXPECT_FALSE(mqtt_client->isConnected());
}

/**
 * Test 6: Service startup sequence simulation
 *
 * This simulates the actual HMS-NUT startup flow to ensure HTTP server
 * would start correctly after subscriptions
 */
TEST_F(AsyncSubscriptionTest, ServiceStartupSequenceNonBlocking) {
    SKIP_WITHOUT_TEST_BROKER();
    auto mqtt_client = std::make_shared<MqttClient>(client_id_ + "_startup");

    std::string url  = mqtt_test_url();
    std::string user = mqtt_test_user();
    std::string pass = mqtt_test_password();

    ASSERT_TRUE(mqtt_client->connect(url, user, pass));

    auto start = std::chrono::high_resolution_clock::now();

    // Simulate HMS-NUT startup sequence

    // Step 1: Start services (non-blocking)
    // (In real code: g_nut_bridge->start(), g_collector->start())

    // Step 2: Setup subscriptions (should be non-blocking now)
    bool sub1 = mqtt_client->subscribe("homeassistant/status",
        [](const std::string& topic, const std::string& payload) {
            // Simulate republish logic
        }, 1);

    bool sub2 = mqtt_client->subscribe("homeassistant/sensor/test_device/+/state",
        [](const std::string& topic, const std::string& payload) {
            // Simulate data collection
        }, 1);

    // Step 3: Would start HTTP server here
    // (In real code: drogon::app().run())

    auto end = std::chrono::high_resolution_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    EXPECT_TRUE(sub1 && sub2) << "Both subscriptions should succeed";
    EXPECT_LT(duration_ms, 200) << "Entire startup sequence should complete in < 200ms (was " << duration_ms << "ms)";

    mqtt_client->disconnect();
}

/**
 * Main function for running tests
 */
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
