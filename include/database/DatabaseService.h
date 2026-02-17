#pragma once

#include "nut/UpsData.h"
#include <pqxx/pqxx>
#include <string>
#include <optional>
#include <mutex>
#include <memory>
#include <map>
#include <functional>

namespace hms_nut {

/**
 * DatabaseService - Singleton PostgreSQL database service
 *
 * Handles:
 * - Connection management with auto-reconnect
 * - UPS metrics insertion
 * - Device ID caching
 * - Power event logging
 * - Thread-safe operations
 */
class DatabaseService {
public:
    /**
     * Get singleton instance
     */
    static DatabaseService& getInstance();

    // Delete copy/move
    DatabaseService(const DatabaseService&) = delete;
    DatabaseService& operator=(const DatabaseService&) = delete;

    /**
     * Initialize database connection
     *
     * @param connection_string PostgreSQL connection string
     *                          (e.g., "host=localhost port=5432 dbname=ups_monitoring user=maestro password=...")
     */
    void initialize(const std::string& connection_string);

    /**
     * Check if connected to database
     *
     * @return true if connected
     */
    bool isConnected() const;

    /**
     * Insert UPS metrics (1-hour aggregated data)
     *
     * Inserts into ups_metrics table
     * Uses ON CONFLICT to handle duplicate timestamps
     *
     * @param data UPS data to insert
     * @param device_identifier PostgreSQL device identifier (e.g., "apc_back_ups_xs_1000m")
     * @return true if inserted successfully
     */
    bool insertUpsMetrics(const UpsData& data, const std::string& device_identifier);

    /**
     * Get device_id (primary key) from device_identifier (unique name)
     *
     * Cached for performance
     *
     * @param device_identifier Device identifier string
     * @return Device ID (primary key) or nullopt if not found
     */
    std::optional<int> getDeviceId(const std::string& device_identifier);

    /**
     * Log power event (outage start/end)
     *
     * Inserts into power_events table
     *
     * @param device_id Device ID (from devices table)
     * @param event_type Event type ("outage_start", "outage_end", "battery_low")
     * @param battery_level_start Battery level at start (%)
     * @param battery_level_end Battery level at end (%)
     * @param load_at_event Load percentage at event
     * @return true if logged successfully
     */
    bool logPowerEvent(int device_id,
                       const std::string& event_type,
                       double battery_level_start,
                       double battery_level_end,
                       double load_at_event);

    /**
     * Close database connection
     */
    void close();

private:
    DatabaseService() = default;
    ~DatabaseService();

    /**
     * Reconnect to database
     *
     * @return true if reconnected successfully
     */
    bool reconnect();

    /**
     * Execute operation with retry logic
     *
     * @param operation Function to execute
     * @param max_retries Maximum retry attempts
     * @return true if operation succeeded
     */
    bool executeWithRetry(std::function<bool()> operation, int max_retries = 3);

    /**
     * Load device ID cache from database
     */
    void loadDeviceIdCache();

    // Connection
    std::unique_ptr<pqxx::connection> conn_;
    std::string connection_string_;
    mutable std::mutex connection_mutex_;

    // Device ID cache (device_identifier -> device_id)
    std::map<std::string, int> device_id_cache_;
    mutable std::mutex cache_mutex_;
};

}  // namespace hms_nut
