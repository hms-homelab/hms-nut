#pragma once

#include <mqtt/async_client.h>
#include <atomic>
#include <string>
#include <functional>
#include <mutex>
#include <memory>
#include <map>
#include <vector>

namespace hms_nut {

/**
 * MqttClient - Thread-safe MQTT client wrapper
 *
 * Wraps Eclipse Paho MQTT C++ client with simplified interface for:
 * - Publishing UPS state/discovery messages
 * - Subscribing to multi-device UPS topics
 * - Auto-reconnect on connection loss
 * - Thread-safe operations (shared by multiple service threads)
 *
 * Locking rule: the two mutexes guard this object's own fields only. No Paho
 * call, no wait on a Paho token and no user callback ever runs while either is
 * held. Paho's threads call back into this object (messages, connection lost,
 * reconnected), so holding a lock across a Paho wait would let those callbacks
 * block the very thread the wait depends on. Callbacks may therefore publish,
 * subscribe or unsubscribe freely.
 */
class MqttClient {
public:
    /**
     * Message callback type
     *
     * @param topic MQTT topic
     * @param payload Message payload (string)
     */
    using MessageCallback = std::function<void(const std::string& topic, const std::string& payload)>;

    /**
     * Constructor
     *
     * @param client_id MQTT client identifier (must be unique)
     */
    explicit MqttClient(const std::string& client_id);

    /**
     * Destructor - cleanup and disconnect
     */
    ~MqttClient();

    // Disable copy
    MqttClient(const MqttClient&) = delete;
    MqttClient& operator=(const MqttClient&) = delete;

    /**
     * Connect to MQTT broker
     *
     * @param broker_address Broker address (e.g., "tcp://localhost:1883")
     * @param username MQTT username
     * @param password MQTT password
     * @return true if connected successfully
     */
    bool connect(const std::string& broker_address,
                 const std::string& username,
                 const std::string& password);

    /**
     * Disconnect from broker
     */
    void disconnect();

    /**
     * Check if connected
     *
     * @return true if connected to broker
     */
    bool isConnected() const;

    /**
     * Subscribe to MQTT topic with callback
     *
     * Supports wildcards (+, #)
     *
     * @param topic MQTT topic pattern (e.g., "homeassistant/sensor/+/battery_charge/state")
     * @param callback Function to call when message received
     * @param qos Quality of Service (default: 1)
     * @return true if subscribed successfully
     */
    bool subscribe(const std::string& topic, MessageCallback callback, int qos = 1);

    /**
     * Subscribe to multiple topics with same callback
     *
     * @param topics Vector of MQTT topic patterns
     * @param callback Function to call when message received
     * @param qos Quality of Service (default: 1)
     * @return true if all subscriptions succeeded
     */
    bool subscribeMultiple(const std::vector<std::string>& topics,
                           MessageCallback callback,
                           int qos = 1);

    /**
     * Unsubscribe from topic
     *
     * @param topic MQTT topic pattern
     * @return true if unsubscribed successfully
     */
    bool unsubscribe(const std::string& topic);

    /**
     * Publish to MQTT topic
     *
     * @param topic MQTT topic
     * @param payload Message payload (string)
     * @param qos Quality of Service (0, 1, or 2)
     * @param retain Retain message flag
     * @return true if published successfully
     */
    bool publish(const std::string& topic, const std::string& payload, int qos = 1, bool retain = false);

    /**
     * Get broker address
     *
     * @return Broker address string
     */
    std::string getBrokerAddress() const { return broker_address_; }

private:
    /**
     * Message arrived callback (internal)
     */
    void onMessageArrived(mqtt::const_message_ptr msg);

    /**
     * Connection lost callback (internal)
     */
    void onConnectionLost(const std::string& cause);

    /**
     * Reconnected callback (internal): restores connected_ and re-subscribes
     * every recorded topic, without holding any lock across the Paho calls
     */
    void onReconnected(const std::string& cause);

    /**
     * Check if topic matches pattern (supports wildcards)
     *
     * @param topic Actual topic
     * @param pattern Pattern with wildcards
     * @return true if topic matches pattern
     */
    bool topicMatches(const std::string& topic, const std::string& pattern) const;

    /**
     * The current Paho client, read under connection_mutex_. Callers use the
     * returned pointer after the lock is released.
     */
    std::shared_ptr<mqtt::async_client> currentClient() const;

    // MQTT client (shared so a caller can use it without holding the lock)
    std::shared_ptr<mqtt::async_client> client_;
    std::string client_id_;

    // Message callbacks (map: topic_pattern -> callback)
    std::map<std::string, MessageCallback> message_callbacks_;
    mutable std::mutex callbacks_mutex_;  // Guards message_callbacks_ only

    // Connection state
    std::string broker_address_;
    std::string username_;
    std::string password_;
    std::atomic<bool> connected_;
    std::atomic<bool> initial_connect_done_{false};
    mutable std::mutex connection_mutex_;  // Guards client_ and the broker fields only

    // Auto-reconnect enabled
    bool auto_reconnect_;
};

}  // namespace hms_nut
