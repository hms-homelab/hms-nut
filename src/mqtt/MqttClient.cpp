#include "mqtt/MqttClient.h"
#include <iostream>
#include <sstream>
#include <algorithm>

namespace hms_nut {

MqttClient::MqttClient(const std::string& client_id)
    : client_id_(client_id),
      connected_(false),
      auto_reconnect_(true) {
    std::cout << "📡 MQTT: Initialized with client_id: " << client_id << std::endl;
}

MqttClient::~MqttClient() {
    disconnect();
}

std::shared_ptr<mqtt::async_client> MqttClient::currentClient() const {
    std::lock_guard<std::mutex> lock(connection_mutex_);
    return client_;
}

bool MqttClient::connect(const std::string& broker_address,
                         const std::string& username,
                         const std::string& password) {
    std::cout << "📡 MQTT: Connecting to " << broker_address << "..." << std::endl;

    // A client this call replaces is destroyed only when this function
    // returns, after the lock below is released: destroying it is a Paho call.
    std::shared_ptr<mqtt::async_client> replaced;

    try {
        // Create client with unique ID + timestamp. It is built and wired up
        // before it is published to other threads, without our lock.
        std::string full_client_id = client_id_ + "_" + std::to_string(std::time(nullptr));
        auto client = std::make_shared<mqtt::async_client>(broker_address, full_client_id);

        // Set callbacks
        client->set_message_callback([this](mqtt::const_message_ptr msg) {
            onMessageArrived(msg);
        });

        client->set_connection_lost_handler([this](const std::string& cause) {
            onConnectionLost(cause);
        });

        client->set_connected_handler([this](const std::string& cause) {
            if (initial_connect_done_) {
                onReconnected(cause);
            }
        });

        {
            std::lock_guard<std::mutex> lock(connection_mutex_);
            broker_address_ = broker_address;
            username_ = username;
            password_ = password;
            replaced = std::move(client_);
            client_ = client;
        }

        // Connection options
        mqtt::connect_options connOpts;
        connOpts.set_keep_alive_interval(60);  // 60 seconds keep-alive
        connOpts.set_clean_session(true);
        connOpts.set_user_name(username);
        connOpts.set_password(password);

        // Enable auto-reconnect with exponential backoff
        // Min delay: 1 second, Max delay: 64 seconds
        connOpts.set_automatic_reconnect(1, 64);

        // Connect (blocking). No lock is held while waiting: Paho's thread
        // runs our callbacks around the CONNACK, and they must not block.
        mqtt::token_ptr conntok = client->connect(connOpts);
        conntok->wait();  // Wait for connection

        connected_ = true;
        initial_connect_done_ = true;
        std::cout << "✅ MQTT: Connected successfully" << std::endl;

        return true;

    } catch (const mqtt::exception& e) {
        std::cerr << "❌ MQTT: Connection failed: " << e.what() << std::endl;
        connected_ = false;
        return false;
    }
}

void MqttClient::disconnect() {
    auto client = currentClient();

    // exchange(): only one caller disconnects, and from here on publish(),
    // subscribe() and unsubscribe() refuse instead of racing Paho's
    // disconnect. A publish queued mid-disconnect made Paho report a lost
    // connection afterwards and stalled the next client's connect in tests.
    if (client && connected_.exchange(false)) {
        try {
            std::cout << "📡 MQTT: Disconnecting..." << std::endl;
            // No lock while waiting: Paho may be delivering into a callback
            // that publishes, and the disconnect cannot complete until it
            // returns.
            client->disconnect()->wait();
            std::cout << "📡 MQTT: Disconnected" << std::endl;
        } catch (const mqtt::exception& e) {
            std::cerr << "❌ MQTT: Disconnect error: " << e.what() << std::endl;
            connected_ = client->is_connected();
        }
    }
}

bool MqttClient::isConnected() const {
    if (!connected_) return false;
    auto client = currentClient();
    return client && client->is_connected();
}

bool MqttClient::subscribe(const std::string& topic, MessageCallback callback, int qos) {
    if (!isConnected()) {
        std::cerr << "❌ MQTT: Not connected, cannot subscribe" << std::endl;
        return false;
    }

    auto client = currentClient();
    if (!client) {
        std::cerr << "❌ MQTT: Not connected, cannot subscribe" << std::endl;
        return false;
    }

    // Record the callback before subscribing, so the first message (a
    // retained one arrives right after the SUBACK) is never missed and a
    // reconnect re-subscribes it. The lock covers the table only.
    bool added = false;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        added = message_callbacks_.count(topic) == 0;
        message_callbacks_[topic] = callback;
    }

    try {
        std::cout << "📡 MQTT: Subscribing to: " << topic << " (QoS " << qos << ")" << std::endl;

        // Initiate async subscribe without any lock held, and don't wait for
        // the SUBACK (callers include the HTTP server's threads)
        client->subscribe(topic, qos);
        std::cout << "✅ MQTT: Subscription initiated for " << topic << " (async)" << std::endl;

        return true;

    } catch (const mqtt::exception& e) {
        std::cerr << "❌ MQTT: Subscribe failed: " << e.what() << std::endl;
        // Not subscribed: forget the callback this call added, so the caller
        // sees the failure and a reconnect does not resurrect it.
        if (added) {
            std::lock_guard<std::mutex> lock(callbacks_mutex_);
            message_callbacks_.erase(topic);
        }
        return false;
    }
}

bool MqttClient::subscribeMultiple(const std::vector<std::string>& topics,
                                    MessageCallback callback,
                                    int qos) {
    bool all_success = true;

    for (const auto& topic : topics) {
        if (!subscribe(topic, callback, qos)) {
            all_success = false;
            std::cerr << "⚠️  MQTT: Failed to subscribe to: " << topic << std::endl;
        }
    }

    return all_success;
}

bool MqttClient::unsubscribe(const std::string& topic) {
    if (!isConnected()) {
        std::cerr << "❌ MQTT: Not connected, cannot unsubscribe" << std::endl;
        return false;
    }

    auto client = currentClient();
    if (!client) {
        std::cerr << "❌ MQTT: Not connected, cannot unsubscribe" << std::endl;
        return false;
    }

    try {
        // Wait for the UNSUBACK with no lock held: Paho's receive thread
        // reads it, and may first be running a callback that publishes.
        client->unsubscribe(topic)->wait();

        // Remove callback
        {
            std::lock_guard<std::mutex> lock(callbacks_mutex_);
            message_callbacks_.erase(topic);
        }

        std::cout << "📡 MQTT: Unsubscribed from " << topic << std::endl;
        return true;

    } catch (const mqtt::exception& e) {
        std::cerr << "❌ MQTT: Unsubscribe failed: " << e.what() << std::endl;
        return false;
    }
}

bool MqttClient::publish(const std::string& topic,
                         const std::string& payload,
                         int qos,
                         bool retain) {
    if (!isConnected()) {
        std::cerr << "❌ MQTT: Not connected, cannot publish" << std::endl;
        return false;
    }

    auto client = currentClient();
    if (!client) {
        std::cerr << "❌ MQTT: Not connected, cannot publish" << std::endl;
        return false;
    }

    try {
        mqtt::message_ptr pubmsg = mqtt::make_message(topic, payload);
        pubmsg->set_qos(qos);
        pubmsg->set_retained(retain);

        // Publish asynchronously without waiting, and without any lock held
        client->publish(pubmsg);

        // Simplified logging for state messages (too verbose otherwise)
        if (topic.find("/state") != std::string::npos) {
            // Only log occasionally for state messages (publish() runs on
            // several threads, hence the atomic)
            static std::atomic<int> log_counter{0};
            int published = ++log_counter;
            if (published % 50 == 0) {  // Log every 50th message
                std::cout << "📤 MQTT: Published " << published << " messages..." << std::endl;
            }
        } else {
            std::cout << "📤 MQTT: Published to " << topic
                      << " (" << payload.length() << " bytes)"
                      << (retain ? " [retained]" : "") << std::endl;
        }

        return true;

    } catch (const mqtt::exception& e) {
        std::cerr << "❌ MQTT: Publish failed: " << e.what() << std::endl;
        return false;
    }
}

bool MqttClient::topicMatches(const std::string& topic, const std::string& pattern) const {
    // Split topic and pattern by '/'
    auto split = [](const std::string& str) -> std::vector<std::string> {
        std::vector<std::string> parts;
        std::stringstream ss(str);
        std::string part;
        while (std::getline(ss, part, '/')) {
            parts.push_back(part);
        }
        return parts;
    };

    auto topic_parts = split(topic);
    auto pattern_parts = split(pattern);

    // Check if pattern has '#' wildcard (multi-level)
    bool has_multilevel = !pattern_parts.empty() && pattern_parts.back() == "#";

    if (has_multilevel) {
        // Match up to '#'
        if (topic_parts.size() < pattern_parts.size() - 1) {
            return false;
        }
        pattern_parts.pop_back();  // Remove '#'
    } else {
        // Exact level count match required
        if (topic_parts.size() != pattern_parts.size()) {
            return false;
        }
    }

    // Match each level
    for (size_t i = 0; i < pattern_parts.size(); ++i) {
        const auto& pattern_level = pattern_parts[i];
        const auto& topic_level = topic_parts[i];

        if (pattern_level == "+") {
            // Single-level wildcard - matches any value
            continue;
        } else if (pattern_level != topic_level) {
            // Exact match required
            return false;
        }
    }

    return true;
}

void MqttClient::onMessageArrived(mqtt::const_message_ptr msg) {
    std::string topic = msg->get_topic();
    std::string payload = msg->to_string();

    // Find matching callbacks under the lock, run them without it: a
    // callback may subscribe, unsubscribe or publish.
    std::vector<MessageCallback> matching;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        for (const auto& [pattern, callback] : message_callbacks_) {
            if (topicMatches(topic, pattern)) {
                matching.push_back(callback);
            }
        }
    }

    for (const auto& callback : matching) {
        try {
            callback(topic, payload);
        } catch (const std::exception& e) {
            std::cerr << "❌ MQTT: Callback error for topic " << topic
                      << ": " << e.what() << std::endl;
        }
    }
}

void MqttClient::onConnectionLost(const std::string& cause) {
    connected_ = false;

    std::cerr << "⚠️  MQTT: Connection lost: " << cause << std::endl;

    if (auto_reconnect_) {
        std::cout << "🔄 MQTT: Auto-reconnect enabled (handled by paho-mqtt)" << std::endl;
    }
}

void MqttClient::onReconnected(const std::string& cause) {
    // Runs on Paho's thread. It takes each lock only long enough to copy, and
    // that is safe because no thread ever holds one while waiting on Paho.
    connected_ = true;
    std::cout << "✅ MQTT: Reconnected: " << cause << std::endl;

    std::vector<std::string> topics;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        for (const auto& [topic, callback] : message_callbacks_) {
            topics.push_back(topic);
        }
    }
    auto client = currentClient();
    if (!client) return;

    // Re-subscribe without waiting and without any lock held
    for (const auto& topic : topics) {
        try {
            client->subscribe(topic, 1);  // No ->wait()
            std::cout << "✅ MQTT: Re-subscribed to " << topic << std::endl;
        } catch (const mqtt::exception& e) {
            std::cerr << "❌ MQTT: Re-subscribe failed for " << topic << ": " << e.what() << std::endl;
        }
    }
}

}  // namespace hms_nut
