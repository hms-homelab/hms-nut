#include "nut/UpsData.h"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <iostream>

namespace hms_nut {

namespace {
    // Helper to safely parse double
    std::optional<double> parseDouble(const std::string& str) {
        try {
            size_t pos;
            double value = std::stod(str, &pos);
            return (pos > 0) ? std::optional<double>(value) : std::nullopt;
        } catch (...) {
            return std::nullopt;
        }
    }

    // Helper to safely parse int
    std::optional<int> parseInt(const std::string& str) {
        try {
            size_t pos;
            int value = std::stoi(str, &pos);
            return (pos > 0) ? std::optional<int>(value) : std::nullopt;
        } catch (...) {
            return std::nullopt;
        }
    }

    // Helper to get string value from map
    std::optional<std::string> getString(const std::map<std::string, std::string>& vars,
                                         const std::string& key) {
        auto it = vars.find(key);
        if (it != vars.end() && !it->second.empty()) {
            return it->second;
        }
        return std::nullopt;
    }
}

UpsData UpsData::fromNutVariables(const std::string& device_id,
                                  const std::map<std::string, std::string>& vars) {
    UpsData data;
    data.device_id = device_id;
    data.timestamp = std::chrono::system_clock::now();

    // Battery metrics
    if (auto val = getString(vars, "battery.charge")) {
        data.battery_charge = parseDouble(*val);
    }
    if (auto val = getString(vars, "battery.voltage")) {
        data.battery_voltage = parseDouble(*val);
    }
    if (auto val = getString(vars, "battery.runtime")) {
        data.battery_runtime = parseInt(*val);
    }
    if (auto val = getString(vars, "battery.voltage.nominal")) {
        data.battery_nominal_voltage = parseDouble(*val);
    }
    if (auto val = getString(vars, "battery.charge.low")) {
        data.battery_low_threshold = parseDouble(*val);
    }
    if (auto val = getString(vars, "battery.charge.warning")) {
        data.battery_warning_threshold = parseDouble(*val);
    }
    data.battery_type = getString(vars, "battery.type");
    data.battery_mfr_date = getString(vars, "battery.mfr.date");

    // Input metrics
    if (auto val = getString(vars, "input.voltage")) {
        data.input_voltage = parseDouble(*val);
    }
    if (auto val = getString(vars, "input.voltage.nominal")) {
        data.input_nominal_voltage = parseInt(*val);
    }
    if (auto val = getString(vars, "input.transfer.high")) {
        data.high_voltage_transfer = parseDouble(*val);
    }
    if (auto val = getString(vars, "input.transfer.low")) {
        data.low_voltage_transfer = parseDouble(*val);
    }
    data.input_sensitivity = getString(vars, "input.sensitivity");
    data.last_transfer_reason = getString(vars, "input.transfer.reason");

    // Load & status
    if (auto val = getString(vars, "ups.load")) {
        data.load_percentage = parseDouble(*val);
        // Calculate load watts (assuming 600W nominal)
        if (data.load_percentage) {
            data.load_watts = (*data.load_percentage / 100.0) * 600.0;
        }
    }

    data.ups_status = getString(vars, "ups.status");
    if (data.ups_status) {
        // Check if "OB" (On Battery) is in status
        data.power_failure = (data.ups_status->find("OB") != std::string::npos);
    }

    // UPS info
    if (auto val = getString(vars, "ups.realpower.nominal")) {
        data.ups_nominal_power = parseDouble(*val);
    }
    data.beeper_status = getString(vars, "ups.beeper.status");
    data.self_test_result = getString(vars, "ups.test.result");
    data.firmware_version = getString(vars, "ups.firmware");

    if (auto val = getString(vars, "ups.delay.shutdown")) {
        data.delay_shutdown = parseInt(*val);
    }
    if (auto val = getString(vars, "ups.timer.reboot")) {
        data.timer_reboot = parseInt(*val);
    }
    if (auto val = getString(vars, "ups.timer.shutdown")) {
        data.timer_shutdown = parseInt(*val);
    }

    // Driver
    data.driver_name = getString(vars, "driver.name");
    data.driver_version = getString(vars, "driver.version");
    data.driver_state = getString(vars, "driver.state");

    // Temperature
    if (auto val = getString(vars, "ups.temperature")) {
        data.temperature = parseDouble(*val);
    }

    // Output voltage
    if (auto val = getString(vars, "output.voltage")) {
        data.output_voltage = parseDouble(*val);
    }
    if (auto val = getString(vars, "output.voltage.nominal")) {
        data.output_nominal_voltage = parseInt(*val);
    }

    return data;
}

void UpsData::updateFieldFromMqtt(const std::string& sensor_name, const std::string& value) {
    // Update timestamp
    timestamp = std::chrono::system_clock::now();

    // Map sensor names to fields
    if (sensor_name == "battery_charge") {
        battery_charge = parseDouble(value);
    } else if (sensor_name == "battery_voltage") {
        battery_voltage = parseDouble(value);
    } else if (sensor_name == "battery_runtime") {
        battery_runtime = parseInt(value);
    } else if (sensor_name == "battery_nominal_voltage" || sensor_name == "battery_voltage_nominal") {
        // Docker NUT: battery_nominal_voltage, ESP32: battery_voltage_nominal
        battery_nominal_voltage = parseDouble(value);
    } else if (sensor_name == "battery_low_charge_threshold" || sensor_name == "battery_charge_low") {
        // Docker NUT: battery_low_charge_threshold, ESP32: battery_charge_low
        battery_low_threshold = parseDouble(value);
    } else if (sensor_name == "battery_warning_charge_threshold" || sensor_name == "battery_charge_warning") {
        // Docker NUT: battery_warning_charge_threshold, ESP32: battery_charge_warning
        battery_warning_threshold = parseDouble(value);
    } else if (sensor_name == "input_voltage") {
        input_voltage = parseDouble(value);
    } else if (sensor_name == "input_nominal_voltage" || sensor_name == "input_voltage_nominal") {
        // Docker NUT: input_nominal_voltage, ESP32: input_voltage_nominal
        input_nominal_voltage = parseInt(value);
    } else if (sensor_name == "high_voltage_transfer" || sensor_name == "input_transfer_high") {
        // Docker NUT: high_voltage_transfer, ESP32: input_transfer_high
        high_voltage_transfer = parseDouble(value);
    } else if (sensor_name == "low_voltage_transfer" || sensor_name == "input_transfer_low") {
        // Docker NUT: low_voltage_transfer, ESP32: input_transfer_low
        low_voltage_transfer = parseDouble(value);
    } else if (sensor_name == "load_percentage" || sensor_name == "load_percent") {
        // Docker NUT: load_percentage, ESP32: load_percent
        load_percentage = parseDouble(value);
        if (load_percentage) {
            load_watts = (*load_percentage / 100.0) * 600.0;
        }
    } else if (sensor_name == "load_watts") {
        load_watts = parseDouble(value);
    } else if (sensor_name == "ups_status" || sensor_name == "status") {
        // Accept both "ups_status" (Docker NUT) and "status" (ESP32)
        ups_status = value;
        power_failure = (value.find("OB") != std::string::npos);
    } else if (sensor_name == "power_failure") {
        power_failure = (value == "1" || value == "true" || value == "on");
    } else if (sensor_name == "ups_nominal_power") {
        ups_nominal_power = parseDouble(value);
    } else if (sensor_name == "temperature") {
        temperature = parseDouble(value);
    } else if (sensor_name == "output_voltage") {
        output_voltage = parseDouble(value);
    } else if (sensor_name == "output_nominal_voltage") {
        output_nominal_voltage = parseInt(value);
    } else if (sensor_name == "beeper_status") {
        beeper_status = value;
    } else if (sensor_name == "self_test_result") {
        self_test_result = value;
    } else if (sensor_name == "firmware_version") {
        firmware_version = value;
    } else if (sensor_name == "driver_name") {
        driver_name = value;
    } else if (sensor_name == "driver_version") {
        driver_version = value;
    } else if (sensor_name == "driver_state") {
        driver_state = value;
    } else if (sensor_name == "input_sensitivity") {
        input_sensitivity = value;
    } else if (sensor_name == "last_transfer_reason" || sensor_name == "input_transfer_reason") {
        // Docker NUT: last_transfer_reason, ESP32: input_transfer_reason
        last_transfer_reason = value;
    }
}

bool UpsData::isValid() const {
    // At minimum, we need battery charge and UPS status
    return battery_charge.has_value() && ups_status.has_value();
}

std::string UpsData::toJson() const {
    Json::Value root;
    root["device_id"] = device_id;

    auto time_t_val = std::chrono::system_clock::to_time_t(timestamp);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t_val), "%Y-%m-%dT%H:%M:%SZ");
    root["timestamp"] = oss.str();

    if (battery_charge) root["battery_charge"] = *battery_charge;
    if (battery_voltage) root["battery_voltage"] = *battery_voltage;
    if (battery_runtime) root["battery_runtime"] = *battery_runtime;
    if (input_voltage) root["input_voltage"] = *input_voltage;
    if (load_percentage) root["load_percentage"] = *load_percentage;
    if (load_watts) root["load_watts"] = *load_watts;
    if (ups_status) root["ups_status"] = *ups_status;
    if (power_failure) root["power_failure"] = *power_failure;
    if (temperature) root["temperature"] = *temperature;

    Json::StreamWriterBuilder builder;
    return Json::writeString(builder, root);
}

std::vector<MqttMessage> UpsData::toMqttMessages() const {
    std::vector<MqttMessage> messages;
    std::string base_topic = "homeassistant/sensor/" + device_id;

    auto addMessage = [&](const std::string& sensor, const std::string& value) {
        messages.push_back({
            base_topic + "/" + sensor + "/state",
            value,
            1,  // QoS 1
            false  // Not retained
        });
    };

    // Battery metrics
    if (battery_charge) {
        addMessage("battery_charge", std::to_string(*battery_charge));
    }
    if (battery_voltage) {
        addMessage("battery_voltage", std::to_string(*battery_voltage));
    }
    if (battery_runtime) {
        addMessage("battery_runtime", std::to_string(*battery_runtime));
    }
    if (battery_nominal_voltage) {
        addMessage("battery_nominal_voltage", std::to_string(*battery_nominal_voltage));
    }
    if (battery_low_threshold) {
        addMessage("battery_low_charge_threshold", std::to_string(*battery_low_threshold));
    }
    if (battery_warning_threshold) {
        addMessage("battery_warning_charge_threshold", std::to_string(*battery_warning_threshold));
    }

    // Input metrics
    if (input_voltage) {
        addMessage("input_voltage", std::to_string(*input_voltage));
    }
    if (input_nominal_voltage) {
        addMessage("input_nominal_voltage", std::to_string(*input_nominal_voltage));
    }
    if (high_voltage_transfer) {
        addMessage("high_voltage_transfer", std::to_string(*high_voltage_transfer));
    }
    if (low_voltage_transfer) {
        addMessage("low_voltage_transfer", std::to_string(*low_voltage_transfer));
    }
    if (input_sensitivity) {
        addMessage("input_sensitivity", *input_sensitivity);
    }
    if (last_transfer_reason) {
        addMessage("last_transfer_reason", *last_transfer_reason);
    }

    // Load & status
    if (load_percentage) {
        addMessage("load_percentage", std::to_string(*load_percentage));
    }
    if (load_watts) {
        addMessage("load_watts", std::to_string(*load_watts));
    }
    if (ups_status) {
        addMessage("ups_status", *ups_status);
    }
    if (power_failure) {
        addMessage("power_failure", *power_failure ? "1" : "0");
    }

    // UPS info
    if (ups_nominal_power) {
        addMessage("ups_nominal_power", std::to_string(*ups_nominal_power));
    }
    if (beeper_status) {
        addMessage("beeper_status", *beeper_status);
    }
    if (self_test_result) {
        addMessage("self_test_result", *self_test_result);
    }
    if (firmware_version) {
        addMessage("firmware_version", *firmware_version);
    }

    // Driver
    if (driver_name) {
        addMessage("driver_name", *driver_name);
    }
    if (driver_version) {
        addMessage("driver_version", *driver_version);
    }
    if (driver_state) {
        addMessage("driver_state", *driver_state);
    }

    // Temperature
    if (temperature) {
        addMessage("temperature", std::to_string(*temperature));
    }

    // Output voltage
    if (output_voltage) {
        addMessage("output_voltage", std::to_string(*output_voltage));
    }
    if (output_nominal_voltage) {
        addMessage("output_nominal_voltage", std::to_string(*output_nominal_voltage));
    }

    return messages;
}

}  // namespace hms_nut
