#pragma once

#include <string>
#include <map>
#include <vector>
#include <mutex>

namespace hms_nut {

/**
 * DeviceConfig - Configuration for a single UPS device
 */
struct DeviceConfig {
    std::string mqtt_device_id;     // MQTT topic prefix (e.g., "apc_bx")
    std::string db_identifier;      // PostgreSQL identifier
    std::string friendly_name;      // Human-readable name
};

/**
 * DeviceMapper - Maps MQTT device IDs to PostgreSQL device identifiers
 *
 * Now supports runtime configuration via environment variables:
 * - UPS_DEVICE_IDS: Comma-separated list of MQTT device prefixes
 * - UPS_DB_MAPPING: JSON object mapping MQTT IDs to DB identifiers
 * - UPS_FRIENDLY_NAMES: JSON object mapping MQTT IDs to friendly names
 */
class DeviceMapper {
public:
    /**
     * Initialize device mappings from environment variables
     * Call this once at startup before using other methods
     */
    static void initialize();

    /**
     * Get all configured device IDs (MQTT topic prefixes)
     *
     * @return Vector of MQTT device IDs
     */
    static std::vector<std::string> getDeviceIds();

    /**
     * Get PostgreSQL device_identifier from MQTT device_id
     *
     * @param mqtt_device_id Device ID from MQTT topic (e.g., "apc_bx")
     * @return Database identifier (e.g., "apc_back_ups_xs_1000m")
     */
    static std::string getDbIdentifier(const std::string& mqtt_device_id);

    /**
     * Get MQTT device_id from PostgreSQL device_identifier
     *
     * @param db_identifier Database identifier
     * @return MQTT device ID
     */
    static std::string getMqttDeviceId(const std::string& db_identifier);

    /**
     * Get friendly name for device
     *
     * @param mqtt_device_id MQTT device ID
     * @return Human-readable device name
     */
    static std::string getFriendlyName(const std::string& mqtt_device_id);

    /**
     * Check if device ID is recognized
     *
     * @param mqtt_device_id MQTT device ID
     * @return true if device is known
     */
    static bool isKnownDevice(const std::string& mqtt_device_id);

    /**
     * Add or update a device configuration at runtime
     *
     * @param config Device configuration
     */
    static void addDevice(const DeviceConfig& config);

    /**
     * Reset all mappings (primarily for testing)
     */
    static void reset();

private:
    static void parseDeviceIds(const std::string& device_ids_str);
    static void parseDbMapping(const std::string& mapping_json);
    static void parseFriendlyNames(const std::string& names_json);

    // Mapping: MQTT device_id -> DB device_identifier
    static std::map<std::string, std::string> mqtt_to_db_map_;

    // Reverse mapping: DB device_identifier -> MQTT device_id
    static std::map<std::string, std::string> db_to_mqtt_map_;

    // Friendly names
    static std::map<std::string, std::string> friendly_names_;

    // Ordered list of device IDs
    static std::vector<std::string> device_ids_;

    // Thread safety
    static std::mutex mutex_;
    static bool initialized_;
};

}  // namespace hms_nut
