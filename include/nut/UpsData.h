#pragma once

#include <string>
#include <optional>
#include <chrono>
#include <map>
#include <vector>
#include <json/json.h>

namespace hms_nut {

struct MqttMessage {
    std::string topic;
    std::string payload;
    int qos;
    bool retain;
};

struct UpsData {
    std::string device_id;  // MQTT topic prefix (e.g., "apc_ups")
    std::chrono::system_clock::time_point timestamp;

    // Battery metrics
    std::optional<double> battery_charge;
    std::optional<double> battery_voltage;
    std::optional<int> battery_runtime;
    std::optional<double> battery_nominal_voltage;
    std::optional<double> battery_low_threshold;
    std::optional<double> battery_warning_threshold;
    std::optional<std::string> battery_type;
    std::optional<std::string> battery_mfr_date;

    // Input metrics
    std::optional<double> input_voltage;
    std::optional<int> input_nominal_voltage;
    std::optional<double> high_voltage_transfer;
    std::optional<double> low_voltage_transfer;
    std::optional<std::string> input_sensitivity;
    std::optional<std::string> last_transfer_reason;

    // Load & status
    std::optional<double> load_percentage;
    std::optional<double> load_watts;
    std::optional<std::string> ups_status;
    std::optional<bool> power_failure;

    // UPS info
    std::optional<double> ups_nominal_power;
    std::optional<std::string> beeper_status;
    std::optional<std::string> self_test_result;
    std::optional<std::string> firmware_version;
    std::optional<int> delay_shutdown;
    std::optional<int> timer_reboot;
    std::optional<int> timer_shutdown;

    // Driver
    std::optional<std::string> driver_name;
    std::optional<std::string> driver_version;
    std::optional<std::string> driver_state;

    // Temperature
    std::optional<double> temperature;

    // Output voltage
    std::optional<double> output_voltage;
    std::optional<int> output_nominal_voltage;

    // Factory methods
    static UpsData fromNutVariables(const std::string& device_id,
                                    const std::map<std::string, std::string>& vars);

    // Update single field from MQTT
    void updateFieldFromMqtt(const std::string& sensor_name, const std::string& value);

    // Validation
    bool isValid() const;

    // Serialization
    std::string toJson() const;
    std::vector<MqttMessage> toMqttMessages() const;
};

}  // namespace hms_nut
