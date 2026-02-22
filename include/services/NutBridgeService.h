#pragma once

#include "nut/NutClient.h"
#include "mqtt/MqttClient.h"
#include "mqtt/DiscoveryPublisher.h"
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>

namespace hms_nut {

/**
 * NutBridgeService - Thread 1: NUT Server → MQTT Publisher
 *
 * Polls local NUT server for UPS metrics and publishes to MQTT
 * Runs in background thread with configurable poll interval
 */
class NutBridgeService {
public:
    /**
     * Constructor
     *
     * @param mqtt_client Shared MQTT client
     * @param nut_host NUT server host
     * @param nut_port NUT server port
     * @param ups_name UPS name (e.g., "apc_bx@localhost")
     * @param device_id MQTT device ID (e.g., "apc_ups")
     * @param device_name Friendly device name
     * @param poll_interval_seconds Poll interval in seconds (default: 60)
     */
    NutBridgeService(std::shared_ptr<MqttClient> mqtt_client,
                     const std::string& nut_host,
                     int nut_port,
                     const std::string& ups_name,
                     const std::string& device_id,
                     const std::string& device_name,
                     int poll_interval_seconds = 60);

    /**
     * Destructor - stops service if running
     */
    ~NutBridgeService();

    // Disable copy
    NutBridgeService(const NutBridgeService&) = delete;
    NutBridgeService& operator=(const NutBridgeService&) = delete;

    /**
     * Start the service (background thread)
     */
    void start();

    /**
     * Stop the service gracefully
     */
    void stop();

    /**
     * Check if service is running
     *
     * @return true if background thread is active
     */
    bool isRunning() const;

    /**
     * Get last poll timestamp
     *
     * @return Time point of last successful poll
     */
    std::chrono::system_clock::time_point getLastPollTime() const;

    /**
     * Republish MQTT discovery messages
     *
     * @return true if republish succeeded
     */
    bool republishDiscovery();

    /**
     * Setup MQTT subscriptions (call this BEFORE starting Drogon)
     */
    void setupSubscriptions();

private:
    /**
     * Background thread main loop
     */
    void runLoop();

    /**
     * Poll NUT server once and publish to MQTT
     *
     * @return true if poll and publish succeeded
     */
    bool pollAndPublish();

    // Dependencies
    std::shared_ptr<MqttClient> mqtt_client_;
    std::unique_ptr<NutClient> nut_client_;
    std::unique_ptr<DiscoveryPublisher> discovery_publisher_;

    // Configuration
    std::string device_id_;
    std::string device_name_;
    int poll_interval_seconds_;

    // Thread management
    std::thread worker_thread_;
    std::atomic<bool> running_;

    // Status tracking
    std::chrono::system_clock::time_point last_poll_time_;
    bool discovery_published_;
    mutable std::mutex mutex_;
};

}  // namespace hms_nut
