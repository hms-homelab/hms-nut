#pragma once

#include "mqtt/MqttClient.h"
#include "database/DatabaseService.h"
#include "nut/UpsData.h"
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <map>
#include <chrono>
#include <vector>

namespace hms_nut {

/**
 * CollectorService - Thread 2: MQTT → PostgreSQL Collector
 *
 * Subscribes to MQTT topics from all UPS devices
 * Aggregates metrics in memory
 * Persists to PostgreSQL at configurable intervals (default: 1 hour)
 */
class CollectorService {
public:
    /**
     * Constructor
     *
     * @param mqtt_client Shared MQTT client
     * @param db_service Database service (singleton)
     * @param save_interval_seconds Save interval in seconds (default: 3600 = 1 hour)
     */
    CollectorService(std::shared_ptr<MqttClient> mqtt_client,
                     DatabaseService& db_service,
                     int save_interval_seconds = 3600);

    /**
     * Destructor - stops service if running
     */
    ~CollectorService();

    // Disable copy
    CollectorService(const CollectorService&) = delete;
    CollectorService& operator=(const CollectorService&) = delete;

    /**
     * Start the service (MQTT subscriptions + background saver thread)
     */
    void start();

    /**
     * Stop the service gracefully (flush pending data)
     */
    void stop();

    /**
     * Check if service is running
     *
     * @return true if service is active
     */
    bool isRunning() const;

    /**
     * Get last save timestamp
     *
     * @return Time point of last database save
     */
    std::chrono::system_clock::time_point getLastSaveTime() const;

    /**
     * Get number of devices being monitored
     *
     * @return Device count
     */
    int getDeviceCount() const;

private:
    /**
     * MQTT message callback
     *
     * Called when message arrives on subscribed topics
     *
     * @param topic MQTT topic
     * @param payload Message payload
     */
    void onMqttMessage(const std::string& topic, const std::string& payload);

    /**
     * Save device data to PostgreSQL
     *
     * @param device_identifier Device identifier
     * @return true if saved successfully
     */
    bool saveDeviceData(const std::string& device_identifier);

    /**
     * Background thread for scheduled saves
     */
    void scheduledSaveLoop();

    /**
     * Parse MQTT topic to extract device_id and sensor_name
     *
     * Topic format: homeassistant/sensor/{device_id}/{sensor_name}/state
     *
     * @param topic MQTT topic
     * @return Pair of (device_id, sensor_name)
     */
    std::pair<std::string, std::string> parseTopic(const std::string& topic) const;

    // Dependencies
    std::shared_ptr<MqttClient> mqtt_client_;
    DatabaseService& db_service_;

    // Configuration
    int save_interval_seconds_;

    // In-memory data buffer
    // Key: device_identifier (e.g., "apc_back_ups_xs_1000m")
    // Value: Accumulated UpsData
    std::map<std::string, UpsData> device_data_;
    mutable std::mutex data_mutex_;

    // Last save timestamps per device
    std::map<std::string, std::chrono::system_clock::time_point> last_save_times_;

    // Thread management
    std::thread saver_thread_;
    std::atomic<bool> running_;

    // Status tracking
    std::chrono::system_clock::time_point last_save_time_;
    mutable std::mutex status_mutex_;
};

}  // namespace hms_nut
