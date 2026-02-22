#include "services/CollectorService.h"
#include "utils/DeviceMapper.h"
#include <iostream>
#include <sstream>
#include <algorithm>

namespace hms_nut {

CollectorService::CollectorService(std::shared_ptr<MqttClient> mqtt_client,
                                   DatabaseService& db_service,
                                   int save_interval_seconds)
    : mqtt_client_(mqtt_client),
      db_service_(db_service),
      save_interval_seconds_(save_interval_seconds),
      running_(false) {

    std::cout << "💾 Collector: Initialized (save interval: " << save_interval_seconds_ << "s)" << std::endl;
}

CollectorService::~CollectorService() {
    stop();
}

void CollectorService::start() {
    if (running_) {
        std::cout << "⚠️  Collector: Already running" << std::endl;
        return;
    }

    std::cout << "🚀 Collector: Starting..." << std::endl;
    running_ = true;

    // Start background saver thread (subscriptions done separately via setupSubscriptions())
    saver_thread_ = std::thread(&CollectorService::scheduledSaveLoop, this);

    std::cout << "✅ Collector: Started" << std::endl;
}

void CollectorService::setupSubscriptions() {
    // Build MQTT topic patterns from configured device IDs
    std::vector<std::string> device_ids = DeviceMapper::getDeviceIds();
    std::vector<std::string> topics;

    for (const auto& device_id : device_ids) {
        std::string topic = "homeassistant/sensor/" + device_id + "/+/state";
        topics.push_back(topic);
        std::cout << "   📡 Subscribing to: " << topic << std::endl;
    }

    auto callback = [this](const std::string& topic, const std::string& payload) {
        onMqttMessage(topic, payload);
    };

    // Subscribe to all sensor topics
    if (!mqtt_client_->subscribeMultiple(topics, callback, 1)) {
        std::cerr << "⚠️  Collector: MQTT subscription failed" << std::endl;
    } else {
        std::cout << "✅ Collector: Subscribed to " << topics.size() << " device topic(s)" << std::endl;
    }
}

void CollectorService::stop() {
    if (!running_) {
        return;
    }

    std::cout << "🛑 Collector: Stopping..." << std::endl;
    running_ = false;

    // Wait for saver thread to finish
    if (saver_thread_.joinable()) {
        saver_thread_.join();
    }

    // Flush remaining data
    std::lock_guard<std::mutex> lock(data_mutex_);
    for (const auto& [device_id, data] : device_data_) {
        saveDeviceData(device_id);
    }

    std::cout << "✅ Collector: Stopped" << std::endl;
}

bool CollectorService::isRunning() const {
    return running_;
}

std::chrono::system_clock::time_point CollectorService::getLastSaveTime() const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    return last_save_time_;
}

int CollectorService::getDeviceCount() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return device_data_.size();
}

std::pair<std::string, std::string> CollectorService::parseTopic(const std::string& topic) const {
    // Topic format: homeassistant/sensor/{device_id}/{sensor_name}/state
    // Example: homeassistant/sensor/apc_ups/battery_charge/state

    std::istringstream ss(topic);
    std::vector<std::string> parts;
    std::string part;

    while (std::getline(ss, part, '/')) {
        parts.push_back(part);
    }

    if (parts.size() >= 5) {
        // parts[0] = "homeassistant"
        // parts[1] = "sensor"
        // parts[2] = device_id
        // parts[3] = sensor_name
        // parts[4] = "state"
        return {parts[2], parts[3]};
    }

    return {"", ""};
}

void CollectorService::onMqttMessage(const std::string& topic, const std::string& payload) {
    // Parse topic
    auto [mqtt_device_id, sensor_name] = parseTopic(topic);

    if (mqtt_device_id.empty() || sensor_name.empty()) {
        return;  // Invalid topic format
    }

    // Map to database identifier
    std::string device_identifier = DeviceMapper::getDbIdentifier(mqtt_device_id);

    // Update in-memory buffer
    std::lock_guard<std::mutex> lock(data_mutex_);

    // Initialize device data if not exists
    if (device_data_.find(device_identifier) == device_data_.end()) {
        device_data_[device_identifier] = UpsData();
        device_data_[device_identifier].device_id = mqtt_device_id;
        device_data_[device_identifier].timestamp = std::chrono::system_clock::now();

        // Initialize last save time
        last_save_times_[device_identifier] = std::chrono::system_clock::now();

        std::cout << "📥 Collector: New device detected: " << device_identifier << std::endl;
    }

    // Update field
    device_data_[device_identifier].updateFieldFromMqtt(sensor_name, payload);

    // Debug logging (occasional)
    static int msg_counter = 0;
    if (++msg_counter % 100 == 0) {
        std::cout << "📥 Collector: Received " << msg_counter << " messages from "
                  << device_data_.size() << " devices" << std::endl;
    }
}

bool CollectorService::saveDeviceData(const std::string& device_identifier) {
    // Must be called with data_mutex_ locked

    auto it = device_data_.find(device_identifier);
    if (it == device_data_.end()) {
        return false;
    }

    const UpsData& data = it->second;

    // Validate data
    if (!data.isValid()) {
        std::cerr << "⚠️  Collector: Invalid data for " << device_identifier << ", skipping save" << std::endl;
        return false;
    }

    // Save to database
    bool success = db_service_.insertUpsMetrics(data, device_identifier);

    if (success) {
        // Update last save time
        last_save_times_[device_identifier] = std::chrono::system_clock::now();

        {
            std::lock_guard<std::mutex> status_lock(status_mutex_);
            last_save_time_ = std::chrono::system_clock::now();
        }

        std::cout << "💾 Collector: Saved metrics for " << device_identifier << std::endl;
    }

    return success;
}

void CollectorService::scheduledSaveLoop() {
    std::cout << "🔄 Collector: Saver thread started" << std::endl;

    while (running_) {
        // Check each device for save interval (do this BEFORE sleeping)
        auto now = std::chrono::system_clock::now();

        {
            std::lock_guard<std::mutex> lock(data_mutex_);

            for (const auto& [device_id, data] : device_data_) {
                // Get last save time
                auto last_save_it = last_save_times_.find(device_id);
                if (last_save_it == last_save_times_.end()) {
                    // First save - trigger immediately instead of waiting
                    std::cout << "💾 Collector: Triggering initial save for " << device_id << std::endl;
                    saveDeviceData(device_id);
                    continue;
                }

                auto last_save = last_save_it->second;
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_save).count();

                if (elapsed >= save_interval_seconds_) {
                    std::cout << "💾 Collector: Triggering scheduled save for " << device_id
                              << " (elapsed: " << elapsed << "s)" << std::endl;
                    saveDeviceData(device_id);
                }
            }
        }

        // Sleep for 1 minute, check every second for shutdown
        for (int i = 0; i < 60 && running_; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    std::cout << "🔄 Collector: Saver thread stopped" << std::endl;
}

}  // namespace hms_nut
