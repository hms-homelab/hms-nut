#pragma once

#include "nut/UpsData.h"
#include <pqxx/pqxx>
#include <string>
#include <optional>
#include <mutex>
#include <memory>
#include <map>
#include <vector>
#include <functional>
#include <json/json.h>

namespace hms_nut {

/**
 * DeviceConfigRow - a persisted device-management entry (device_config table)
 */
struct DeviceConfigRow {
    std::string mqtt_device_id;   // MQTT topic prefix (e.g., "apc_ups_e072a1ead480")
    std::string db_identifier;    // PostgreSQL device_identifier
    std::string friendly_name;    // Human-readable name
    bool enabled = true;          // whether the collector should monitor it
};

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
     * Query daily aggregated metrics for all devices on a given date
     *
     * Returns a formatted string with per-device stats:
     * voltage ranges, load, battery, power failures, etc.
     *
     * @param date Date string in YYYY-MM-DD format
     * @return Formatted metrics string, or empty on failure
     */
    std::string queryDailyMetrics(const std::string& date);

    // ─────────────────────────────────────────────────────────────
    // Device configuration (persisted, editable via the web UI)
    // ─────────────────────────────────────────────────────────────

    /**
     * Create the device_config table if it does not exist. Idempotent.
     */
    void ensureDeviceConfigTable();

    /**
     * Seed device_config from the given (env-derived) devices, but only if the
     * table is currently empty. Preserves the existing env-based config as the
     * initial state on first run. Returns the number of rows seeded.
     */
    int seedDeviceConfigFromEnvIfEmpty(const std::vector<DeviceConfigRow>& devices);

    /**
     * List configured devices.
     * @param include_disabled when false, only rows with enabled=true are returned
     */
    std::vector<DeviceConfigRow> listDeviceConfigs(bool include_disabled = true);

    /**
     * Insert or update a device_config row (keyed by mqtt_device_id).
     * Also ensures a matching ups_devices row exists so metrics inserts resolve.
     */
    bool upsertDeviceConfig(const DeviceConfigRow& cfg);

    /**
     * Enable/disable a device without deleting it.
     */
    bool setDeviceEnabled(const std::string& mqtt_device_id, bool enabled);

    /**
     * Delete a device_config row. The ups_devices row (and its history) is kept.
     */
    bool deleteDeviceConfig(const std::string& mqtt_device_id);

    /**
     * Ensure an ups_devices row exists for db_identifier; returns its device_id.
     */
    std::optional<int> ensureUpsDevice(const std::string& db_identifier,
                                       const std::string& friendly_name);

    // ─────────────────────────────────────────────────────────────
    // Read APIs for the web UI
    // ─────────────────────────────────────────────────────────────

    /**
     * Time-series metrics for a device over the last N hours.
     * @return JSON array of {t, battery_charge, load_percentage, input_voltage}
     */
    Json::Value queryHistory(const std::string& db_identifier, int hours);

    /**
     * Most recent power events for a device (or all devices if empty).
     * @return JSON array of {device_name, event_type, timestamp, battery_start, battery_end, load}
     */
    Json::Value queryRecentEvents(const std::string& db_identifier, int limit);

    // ─────────────────────────────────────────────────────────────
    // Daily energy summaries (LLM output, persisted so the web UI can
    // show history instead of only the last one since service start)
    // ─────────────────────────────────────────────────────────────

    /**
     * Create the ups_daily_summaries table if it does not exist. Idempotent.
     */
    void ensureDailySummaryTable();

    /**
     * Store (or replace) the generated summary for a date.
     *
     * @param date    Summary date in YYYY-MM-DD form (primary key)
     * @param summary LLM-generated summary text
     * @param model   Model that produced it (for provenance)
     */
    bool saveDailySummary(const std::string& date,
                          const std::string& summary,
                          const std::string& model);

    /**
     * Most recent daily summaries, newest first.
     * @return JSON array of {date, summary, model, generated_at}
     */
    Json::Value queryDailySummaries(int limit);

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
