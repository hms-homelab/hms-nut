#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "mqtt/MqttClient.h"
#include <thread>
#include <chrono>
#include <atomic>

using namespace hms_nut;

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
    auto mqtt_client = std::make_shared<MqttClient>(client_id_);

    // Connect to broker (using environment variables or defaults)
    std::string broker = std::getenv("MQTT_BROKER") ? std::getenv("MQTT_BROKER") : "192.168.2.15";
    std::string url = "tcp://" + broker + ":1883";
    std::string user = std::getenv("MQTT_USER") ? std::getenv("MQTT_USER") : "aamat";
    std::string pass = std::getenv("MQTT_PASSWORD") ? std::getenv("MQTT_PASSWORD") : "exploracion";

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
    auto mqtt_client = std::make_shared<MqttClient>(client_id_ + "_multi");

    std::string broker = std::getenv("MQTT_BROKER") ? std::getenv("MQTT_BROKER") : "192.168.2.15";
    std::string url = "tcp://" + broker + ":1883";
    std::string user = std::getenv("MQTT_USER") ? std::getenv("MQTT_USER") : "aamat";
    std::string pass = std::getenv("MQTT_PASSWORD") ? std::getenv("MQTT_PASSWORD") : "exploracion";

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
    auto mqtt_client = std::make_shared<MqttClient>(client_id_ + "_retained");

    std::string broker = std::getenv("MQTT_BROKER") ? std::getenv("MQTT_BROKER") : "192.168.2.15";
    std::string url = "tcp://" + broker + ":1883";
    std::string user = std::getenv("MQTT_USER") ? std::getenv("MQTT_USER") : "aamat";
    std::string pass = std::getenv("MQTT_PASSWORD") ? std::getenv("MQTT_PASSWORD") : "exploracion";

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
    auto mqtt_client = std::make_shared<MqttClient>(client_id_ + "_callback");

    std::string broker = std::getenv("MQTT_BROKER") ? std::getenv("MQTT_BROKER") : "192.168.2.15";
    std::string url = "tcp://" + broker + ":1883";
    std::string user = std::getenv("MQTT_USER") ? std::getenv("MQTT_USER") : "aamat";
    std::string pass = std::getenv("MQTT_PASSWORD") ? std::getenv("MQTT_PASSWORD") : "exploracion";

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
 * Verifies thread safety of async subscription mechanism
 */
TEST_F(AsyncSubscriptionTest, ConcurrentSubscriptionsThreadSafe) {
    auto mqtt_client = std::make_shared<MqttClient>(client_id_ + "_concurrent");

    std::string broker = std::getenv("MQTT_BROKER") ? std::getenv("MQTT_BROKER") : "192.168.2.15";
    std::string url = "tcp://" + broker + ":1883";
    std::string user = std::getenv("MQTT_USER") ? std::getenv("MQTT_USER") : "aamat";
    std::string pass = std::getenv("MQTT_PASSWORD") ? std::getenv("MQTT_PASSWORD") : "exploracion";

    ASSERT_TRUE(mqtt_client->connect(url, user, pass));

    std::atomic<int> success_count{0};
    std::atomic<int> callback_count{0};

    auto subscribe_task = [&](int id) {
        std::string topic = "test/async/thread" + std::to_string(id);
        bool result = mqtt_client->subscribe(topic,
            [&callback_count](const std::string& topic, const std::string& payload) {
                callback_count++;
            }, 1);
        if (result) {
            success_count++;
        }
    };

    // Create 5 threads subscribing concurrently
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; i++) {
        threads.emplace_back(subscribe_task, i);
    }

    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }

    // All subscriptions should succeed
    EXPECT_EQ(success_count, 5) << "All 5 concurrent subscriptions should succeed";

    mqtt_client->disconnect();
}

/**
 * Test 6: Service startup sequence simulation
 *
 * This simulates the actual HMS-NUT startup flow to ensure HTTP server
 * would start correctly after subscriptions
 */
TEST_F(AsyncSubscriptionTest, ServiceStartupSequenceNonBlocking) {
    auto mqtt_client = std::make_shared<MqttClient>(client_id_ + "_startup");

    std::string broker = std::getenv("MQTT_BROKER") ? std::getenv("MQTT_BROKER") : "192.168.2.15";
    std::string url = "tcp://" + broker + ":1883";
    std::string user = std::getenv("MQTT_USER") ? std::getenv("MQTT_USER") : "aamat";
    std::string pass = std::getenv("MQTT_PASSWORD") ? std::getenv("MQTT_PASSWORD") : "exploracion";

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
