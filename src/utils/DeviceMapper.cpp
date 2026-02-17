#include "utils/DeviceMapper.h"
#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <iostream>
#include <json/json.h>

namespace hms_nut {

// Static member definitions
std::map<std::string, std::string> DeviceMapper::mqtt_to_db_map_;
std::map<std::string, std::string> DeviceMapper::db_to_mqtt_map_;
std::map<std::string, std::string> DeviceMapper::friendly_names_;
std::vector<std::string> DeviceMapper::device_ids_;
std::mutex DeviceMapper::mutex_;
bool DeviceMapper::initialized_ = false;

void DeviceMapper::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (initialized_) {
        return;
    }

    std::cout << "📋 DeviceMapper: Initializing from environment..." << std::endl;

    // Read environment variables
    const char* device_ids_env = std::getenv("UPS_DEVICE_IDS");
    const char* db_mapping_env = std::getenv("UPS_DB_MAPPING");
    const char* friendly_names_env = std::getenv("UPS_FRIENDLY_NAMES");

    // Parse device IDs (required)
    if (device_ids_env && strlen(device_ids_env) > 0) {
        parseDeviceIds(device_ids_env);
    } else {
        // Default: single device using NUT_DEVICE_ID
        const char* nut_device_id = std::getenv("NUT_DEVICE_ID");
        if (nut_device_id && strlen(nut_device_id) > 0) {
            device_ids_.push_back(nut_device_id);
            std::cout << "   Using NUT_DEVICE_ID as default: " << nut_device_id << std::endl;
        } else {
            device_ids_.push_back("ups");  // Ultimate fallback
            std::cout << "   Using fallback device: ups" << std::endl;
        }
    }

    // Parse DB mappings (optional)
    if (db_mapping_env && strlen(db_mapping_env) > 0) {
        parseDbMapping(db_mapping_env);
    } else {
        // Default: use device ID as DB identifier
        for (const auto& id : device_ids_) {
            mqtt_to_db_map_[id] = id;
            db_to_mqtt_map_[id] = id;
        }
    }

    // Parse friendly names (optional)
    if (friendly_names_env && strlen(friendly_names_env) > 0) {
        parseFriendlyNames(friendly_names_env);
    }

    std::cout << "   Configured " << device_ids_.size() << " device(s):" << std::endl;
    for (const auto& id : device_ids_) {
        // Use internal helpers that don't lock (we already hold the lock)
        std::string db_id = id;
        auto it = mqtt_to_db_map_.find(id);
        if (it != mqtt_to_db_map_.end()) {
            db_id = it->second;
        }

        std::string friendly = id;
        auto fn_it = friendly_names_.find(id);
        if (fn_it != friendly_names_.end()) {
            friendly = fn_it->second;
        } else {
            // Generate default friendly name
            std::replace(friendly.begin(), friendly.end(), '_', ' ');
            if (!friendly.empty()) {
                friendly[0] = std::toupper(friendly[0]);
            }
        }

        std::cout << "     - " << id << " → " << db_id << " (" << friendly << ")" << std::endl;
    }

    initialized_ = true;
}

void DeviceMapper::parseDeviceIds(const std::string& device_ids_str) {
    std::istringstream ss(device_ids_str);
    std::string id;

    while (std::getline(ss, id, ',')) {
        // Trim whitespace
        id.erase(0, id.find_first_not_of(" \t"));
        id.erase(id.find_last_not_of(" \t") + 1);

        if (!id.empty()) {
            device_ids_.push_back(id);
        }
    }
}

void DeviceMapper::parseDbMapping(const std::string& mapping_json) {
    try {
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::istringstream stream(mapping_json);

        if (Json::parseFromStream(builder, stream, &root, &errors)) {
            for (const auto& key : root.getMemberNames()) {
                std::string value = root[key].asString();
                mqtt_to_db_map_[key] = value;
                db_to_mqtt_map_[value] = key;
            }
        } else {
            std::cerr << "⚠️  DeviceMapper: Failed to parse UPS_DB_MAPPING: " << errors << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "⚠️  DeviceMapper: Exception parsing UPS_DB_MAPPING: " << e.what() << std::endl;
    }
}

void DeviceMapper::parseFriendlyNames(const std::string& names_json) {
    try {
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::istringstream stream(names_json);

        if (Json::parseFromStream(builder, stream, &root, &errors)) {
            for (const auto& key : root.getMemberNames()) {
                friendly_names_[key] = root[key].asString();
            }
        } else {
            std::cerr << "⚠️  DeviceMapper: Failed to parse UPS_FRIENDLY_NAMES: " << errors << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "⚠️  DeviceMapper: Exception parsing UPS_FRIENDLY_NAMES: " << e.what() << std::endl;
    }
}

std::vector<std::string> DeviceMapper::getDeviceIds() {
    std::lock_guard<std::mutex> lock(mutex_);
    return device_ids_;
}

std::string DeviceMapper::getDbIdentifier(const std::string& mqtt_device_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = mqtt_to_db_map_.find(mqtt_device_id);
    if (it != mqtt_to_db_map_.end()) {
        return it->second;
    }

    // If not found, return as-is
    return mqtt_device_id;
}

std::string DeviceMapper::getMqttDeviceId(const std::string& db_identifier) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = db_to_mqtt_map_.find(db_identifier);
    if (it != db_to_mqtt_map_.end()) {
        return it->second;
    }

    // If not found, return as-is
    return db_identifier;
}

std::string DeviceMapper::getFriendlyName(const std::string& mqtt_device_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = friendly_names_.find(mqtt_device_id);
    if (it != friendly_names_.end()) {
        return it->second;
    }

    // Generate default friendly name
    std::string name = mqtt_device_id;
    // Replace underscores with spaces and capitalize
    std::replace(name.begin(), name.end(), '_', ' ');
    if (!name.empty()) {
        name[0] = std::toupper(name[0]);
    }
    return name;
}

bool DeviceMapper::isKnownDevice(const std::string& mqtt_device_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    return std::find(device_ids_.begin(), device_ids_.end(), mqtt_device_id) != device_ids_.end();
}

void DeviceMapper::addDevice(const DeviceConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Add to device IDs if not already present
    if (std::find(device_ids_.begin(), device_ids_.end(), config.mqtt_device_id) == device_ids_.end()) {
        device_ids_.push_back(config.mqtt_device_id);
    }

    // Update mappings
    mqtt_to_db_map_[config.mqtt_device_id] = config.db_identifier;
    db_to_mqtt_map_[config.db_identifier] = config.mqtt_device_id;

    if (!config.friendly_name.empty()) {
        friendly_names_[config.mqtt_device_id] = config.friendly_name;
    }
}

void DeviceMapper::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    mqtt_to_db_map_.clear();
    db_to_mqtt_map_.clear();
    friendly_names_.clear();
    device_ids_.clear();
    initialized_ = false;
}

}  // namespace hms_nut
