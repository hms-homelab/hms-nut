#include "services/NutBridgeService.h"
#include "nut/UpsData.h"
#include <iostream>
#include <chrono>

namespace hms_nut {

NutBridgeService::NutBridgeService(std::shared_ptr<MqttClient> mqtt_client,
                                   const std::string& nut_host,
                                   int nut_port,
                                   const std::string& ups_name,
                                   const std::string& device_id,
                                   const std::string& device_name,
                                   int poll_interval_seconds)
    : mqtt_client_(mqtt_client),
      device_id_(device_id),
      device_name_(device_name),
      poll_interval_seconds_(poll_interval_seconds),
      running_(false),
      discovery_published_(false) {

    // Create NUT client
    nut_client_ = std::make_unique<NutClient>(nut_host, nut_port, ups_name);

    // Create discovery publisher
    discovery_publisher_ = std::make_unique<DiscoveryPublisher>(
        mqtt_client_, device_id_, device_name_);

    std::cout << "🔌 NUT Bridge: Initialized for " << device_name_
              << " (poll interval: " << poll_interval_seconds_ << "s)" << std::endl;
}

NutBridgeService::~NutBridgeService() {
    stop();
}

void NutBridgeService::start() {
    if (running_) {
        std::cout << "⚠️  NUT Bridge: Already running" << std::endl;
        return;
    }

    std::cout << "🚀 NUT Bridge: Starting..." << std::endl;
    running_ = true;

    // Start background thread
    worker_thread_ = std::thread(&NutBridgeService::runLoop, this);

    std::cout << "✅ NUT Bridge: Started" << std::endl;
}

void NutBridgeService::stop() {
    if (!running_) {
        return;
    }

    std::cout << "🛑 NUT Bridge: Stopping..." << std::endl;
    running_ = false;

    // Wait for thread to finish
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    std::cout << "✅ NUT Bridge: Stopped" << std::endl;
}

bool NutBridgeService::isRunning() const {
    return running_;
}

std::chrono::system_clock::time_point NutBridgeService::getLastPollTime() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_poll_time_;
}

void NutBridgeService::runLoop() {
    std::cout << "🔄 NUT Bridge: Worker thread started" << std::endl;

    // Connect to NUT server
    if (!nut_client_->connect()) {
        std::cerr << "❌ NUT Bridge: Failed to connect to NUT server, will retry..." << std::endl;
    }

    while (running_) {
        try {
            // Ensure connection
            if (!nut_client_->isConnected()) {
                if (!nut_client_->connect()) {
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                    continue;
                }
            }

            // Poll and publish
            if (pollAndPublish()) {
                std::lock_guard<std::mutex> lock(mutex_);
                last_poll_time_ = std::chrono::system_clock::now();
            }

            // Sleep for poll interval
            for (int i = 0; i < poll_interval_seconds_ && running_; ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }

        } catch (const std::exception& e) {
            std::cerr << "❌ NUT Bridge: Exception in worker thread: " << e.what() << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(5));  // Backoff
        }
    }

    // Disconnect on exit
    nut_client_->disconnect();

    std::cout << "🔄 NUT Bridge: Worker thread stopped" << std::endl;
}

bool NutBridgeService::pollAndPublish() {
    // Get all variables from NUT server
    auto variables = nut_client_->getAllVariables();

    if (variables.empty()) {
        std::cerr << "❌ NUT Bridge: No variables retrieved from NUT server" << std::endl;
        return false;
    }

    // Convert to UpsData
    UpsData ups_data = UpsData::fromNutVariables(device_id_, variables);

    if (!ups_data.isValid()) {
        std::cerr << "⚠️  NUT Bridge: Invalid UPS data received" << std::endl;
        return false;
    }

    // Publish or republish discovery config when MQTT is connected
    // This handles both first poll and reconnection scenarios
    if (mqtt_client_->isConnected()) {
        if (!discovery_published_) {
            std::cout << "📡 NUT Bridge: Publishing discovery configs..." << std::endl;
            if (discovery_publisher_->publishAll()) {
                discovery_published_ = true;
            }
        }
    } else {
        // If MQTT disconnected, mark discovery as unpublished so it will be republished on reconnection
        if (discovery_published_) {
            std::cout << "⚠️  NUT Bridge: MQTT disconnected, will republish discovery on reconnection" << std::endl;
            discovery_published_ = false;
        }
    }

    // Convert to MQTT messages
    auto mqtt_messages = ups_data.toMqttMessages();

    // Publish all messages
    bool all_success = true;
    for (const auto& msg : mqtt_messages) {
        if (!mqtt_client_->publish(msg.topic, msg.payload, msg.qos, msg.retain)) {
            all_success = false;
            std::cerr << "⚠️  NUT Bridge: Failed to publish: " << msg.topic << std::endl;
        }
    }

    if (all_success) {
        static int log_counter = 0;
        if (++log_counter % 10 == 0) {  // Log every 10th poll
            std::cout << "📤 NUT Bridge: Published " << mqtt_messages.size()
                      << " metrics (" << log_counter << " polls)" << std::endl;
        }
    }

    return all_success;
}

}  // namespace hms_nut
