#include "database/DatabaseService.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace hms_nut {

DatabaseService& DatabaseService::getInstance() {
    static DatabaseService instance;
    return instance;
}

DatabaseService::~DatabaseService() {
    close();
}

void DatabaseService::initialize(const std::string& connection_string) {
    std::lock_guard<std::mutex> lock(connection_mutex_);

    connection_string_ = connection_string;

    std::cout << "💾 DB: Initializing connection..." << std::endl;

    try {
        conn_ = std::make_unique<pqxx::connection>(connection_string);

        if (conn_->is_open()) {
            std::cout << "✅ DB: Connected to " << conn_->dbname() << std::endl;

            // Load device ID cache
            loadDeviceIdCache();
        } else {
            std::cerr << "❌ DB: Failed to open connection" << std::endl;
        }

    } catch (const std::exception& e) {
        std::cerr << "❌ DB: Connection error: " << e.what() << std::endl;
        conn_ = nullptr;
    }
}

bool DatabaseService::isConnected() const {
    std::lock_guard<std::mutex> lock(connection_mutex_);
    return conn_ && conn_->is_open();
}

bool DatabaseService::reconnect() {
    std::lock_guard<std::mutex> lock(connection_mutex_);

    std::cout << "🔄 DB: Reconnecting..." << std::endl;

    try {
        // Close existing connection
        if (conn_) {
            conn_.reset();
        }

        // Create new connection
        conn_ = std::make_unique<pqxx::connection>(connection_string_);

        if (conn_->is_open()) {
            std::cout << "✅ DB: Reconnected successfully" << std::endl;
            return true;
        } else {
            std::cerr << "❌ DB: Reconnection failed" << std::endl;
            return false;
        }

    } catch (const std::exception& e) {
        std::cerr << "❌ DB: Reconnection error: " << e.what() << std::endl;
        conn_ = nullptr;
        return false;
    }
}

void DatabaseService::close() {
    std::lock_guard<std::mutex> lock(connection_mutex_);

    if (conn_) {
        try {
            std::cout << "💾 DB: Closing connection..." << std::endl;
            conn_.reset();
        } catch (const std::exception& e) {
            std::cerr << "❌ DB: Close error: " << e.what() << std::endl;
        }
    }
}

bool DatabaseService::executeWithRetry(std::function<bool()> operation, int max_retries) {
    for (int attempt = 0; attempt < max_retries; ++attempt) {
        try {
            if (!isConnected()) {
                if (!reconnect()) {
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    continue;
                }
            }

            // Execute operation
            if (operation()) {
                return true;
            }

        } catch (const pqxx::broken_connection& e) {
            std::cerr << "❌ DB: Connection broken: " << e.what() << std::endl;
            reconnect();

        } catch (const std::exception& e) {
            std::cerr << "❌ DB: Operation error: " << e.what() << std::endl;
        }

        if (attempt < max_retries - 1) {
            std::cout << "🔄 DB: Retrying... (attempt " << (attempt + 2) << "/" << max_retries << ")" << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    std::cerr << "❌ DB: Operation failed after " << max_retries << " attempts" << std::endl;
    return false;
}

void DatabaseService::loadDeviceIdCache() {
    // Must be called with connection_mutex_ already locked

    std::lock_guard<std::mutex> cache_lock(cache_mutex_);
    device_id_cache_.clear();

    try {
        pqxx::work txn(*conn_);

        std::string query = "SELECT device_id, device_identifier FROM ups_devices";
        pqxx::result res = txn.exec(query);

        for (const auto& row : res) {
            int device_id = row["device_id"].as<int>();
            std::string device_identifier = row["device_identifier"].as<std::string>();
            device_id_cache_[device_identifier] = device_id;
        }

        txn.commit();

        std::cout << "💾 DB: Loaded " << device_id_cache_.size() << " devices into cache" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "❌ DB: Failed to load device cache: " << e.what() << std::endl;
    }
}

std::optional<int> DatabaseService::getDeviceId(const std::string& device_identifier) {
    // Check cache first
    {
        std::lock_guard<std::mutex> cache_lock(cache_mutex_);
        auto it = device_id_cache_.find(device_identifier);
        if (it != device_id_cache_.end()) {
            return it->second;
        }
    }

    // Query database
    std::optional<int> result;

    executeWithRetry([&]() -> bool {
        std::lock_guard<std::mutex> lock(connection_mutex_);

        try {
            pqxx::work txn(*conn_);

            std::string query = "SELECT device_id FROM ups_devices WHERE device_identifier = " +
                                txn.quote(device_identifier);
            pqxx::result res = txn.exec(query);

            if (!res.empty()) {
                int device_id = res[0]["device_id"].as<int>();
                result = device_id;

                // Update cache
                {
                    std::lock_guard<std::mutex> cache_lock(cache_mutex_);
                    device_id_cache_[device_identifier] = device_id;
                }
            }

            txn.commit();
            return true;

        } catch (const std::exception& e) {
            std::cerr << "❌ DB: getDeviceId error: " << e.what() << std::endl;
            return false;
        }
    });

    return result;
}

bool DatabaseService::insertUpsMetrics(const UpsData& data, const std::string& device_identifier) {
    // Get device_id
    auto device_id_opt = getDeviceId(device_identifier);
    if (!device_id_opt) {
        std::cerr << "❌ DB: Device not found: " << device_identifier << std::endl;
        return false;
    }

    int device_id = *device_id_opt;

    // Format timestamp for PostgreSQL
    auto time_t_val = std::chrono::system_clock::to_time_t(data.timestamp);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t_val), "%Y-%m-%d %H:%M:%S");
    std::string timestamp_str = oss.str();

    return executeWithRetry([&]() -> bool {
        std::lock_guard<std::mutex> lock(connection_mutex_);

        try {
            pqxx::work txn(*conn_);

            // Build INSERT query with ON CONFLICT
            std::ostringstream query;
            query << "INSERT INTO ups_metrics (device_id, timestamp, "
                  << "battery_charge, battery_voltage, battery_runtime, "
                  << "battery_low_charge_threshold, battery_warning_charge_threshold, "
                  << "input_voltage, input_nominal_voltage, "
                  << "high_voltage_transfer, low_voltage_transfer, input_sensitivity, "
                  << "load_percentage, load_watts, ups_status, power_failure, "
                  << "last_transfer_reason, self_test_result, driver_state, "
                  << "beeper_status, temperature, output_voltage, output_nominal_voltage) "
                  << "VALUES ("
                  << device_id << ", "
                  << txn.quote(timestamp_str) << ", ";

            // Helper to add optional values
            auto addOptional = [&](const auto& opt) {
                if (opt) {
                    query << txn.quote(*opt);
                } else {
                    query << "NULL";
                }
                query << ", ";
            };

            auto addOptionalString = [&](const auto& opt) {
                if (opt) {
                    query << txn.quote(*opt);
                } else {
                    query << "NULL";
                }
                query << ", ";
            };

            // Battery metrics
            addOptional(data.battery_charge);
            addOptional(data.battery_voltage);
            addOptional(data.battery_runtime);
            addOptional(data.battery_low_threshold);
            addOptional(data.battery_warning_threshold);

            // Input metrics
            addOptional(data.input_voltage);
            addOptional(data.input_nominal_voltage);
            addOptional(data.high_voltage_transfer);
            addOptional(data.low_voltage_transfer);
            addOptionalString(data.input_sensitivity);

            // Load & status
            addOptional(data.load_percentage);
            addOptional(data.load_watts);
            addOptionalString(data.ups_status);
            addOptional(data.power_failure);

            // Other metrics
            addOptionalString(data.last_transfer_reason);
            addOptionalString(data.self_test_result);
            addOptionalString(data.driver_state);
            addOptionalString(data.beeper_status);
            addOptional(data.temperature);
            addOptional(data.output_voltage);

            // Last field (no trailing comma)
            if (data.output_nominal_voltage) {
                query << txn.quote(*data.output_nominal_voltage);
            } else {
                query << "NULL";
            }

            query << ") ON CONFLICT (device_id, timestamp) DO UPDATE SET "
                  << "battery_charge = EXCLUDED.battery_charge, "
                  << "battery_voltage = EXCLUDED.battery_voltage, "
                  << "battery_runtime = EXCLUDED.battery_runtime, "
                  << "load_percentage = EXCLUDED.load_percentage, "
                  << "load_watts = EXCLUDED.load_watts, "
                  << "input_voltage = EXCLUDED.input_voltage, "
                  << "ups_status = EXCLUDED.ups_status, "
                  << "power_failure = EXCLUDED.power_failure";

            txn.exec(query.str());
            txn.commit();

            std::cout << "💾 DB: Inserted metrics for " << device_identifier
                      << " at " << timestamp_str << std::endl;

            return true;

        } catch (const std::exception& e) {
            std::cerr << "❌ DB: insertUpsMetrics error: " << e.what() << std::endl;
            return false;
        }
    });
}

std::string DatabaseService::queryDailyMetrics(const std::string& date) {
    std::string result;

    executeWithRetry([&]() -> bool {
        std::lock_guard<std::mutex> lock(connection_mutex_);

        try {
            pqxx::work txn(*conn_);

            std::string query =
                "SELECT d.device_name, d.device_identifier, d.location, "
                "COUNT(*) as readings, "
                "ROUND(AVG(m.input_voltage)::numeric, 1) as avg_voltage, "
                "ROUND(MIN(m.input_voltage)::numeric, 1) as min_voltage, "
                "ROUND(MAX(m.input_voltage)::numeric, 1) as max_voltage, "
                "ROUND(AVG(m.load_percentage)::numeric, 1) as avg_load_pct, "
                "ROUND(AVG(m.load_watts)::numeric, 1) as avg_watts, "
                "ROUND(MAX(m.load_watts)::numeric, 1) as max_watts, "
                "ROUND(AVG(m.battery_charge)::numeric, 1) as avg_battery, "
                "ROUND(MIN(m.battery_charge)::numeric, 1) as min_battery, "
                "ROUND(AVG(m.battery_runtime)::numeric, 0) as avg_runtime_sec, "
                "ROUND(MIN(m.battery_runtime)::numeric, 0) as min_runtime_sec, "
                "ROUND(AVG(m.output_voltage)::numeric, 1) as avg_output_voltage, "
                "ROUND(AVG(m.temperature)::numeric, 1) as avg_temperature, "
                "COUNT(*) FILTER (WHERE m.power_failure = true) as power_failures, "
                "COUNT(DISTINCT m.ups_status) as distinct_statuses, "
                "STRING_AGG(DISTINCT m.ups_status, ', ') as statuses, "
                "STRING_AGG(DISTINCT m.last_transfer_reason, ', ') as transfer_reasons, "
                "MIN(m.timestamp) as first_reading, "
                "MAX(m.timestamp) as last_reading "
                "FROM ups_metrics m "
                "JOIN ups_devices d ON m.device_id = d.device_id "
                "WHERE m.timestamp::date = " + txn.quote(date) + " "
                "GROUP BY d.device_id, d.device_name, d.device_identifier, d.location "
                "ORDER BY d.device_id";

            pqxx::result res = txn.exec(query);
            txn.commit();

            if (res.empty()) {
                result = "No UPS metrics data found for " + date + ".";
                return true;
            }

            std::ostringstream oss;
            oss << "UPS Energy Report for " << date << "\n";
            oss << "========================================\n\n";

            for (const auto& row : res) {
                std::string name = row["device_name"].as<std::string>();
                std::string identifier = row["device_identifier"].as<std::string>();
                int readings = row["readings"].as<int>();

                oss << "Device: " << name << " (" << identifier << ")\n";
                oss << "  Readings: " << readings << "\n";
                oss << "  Time range: " << row["first_reading"].as<std::string>()
                    << " to " << row["last_reading"].as<std::string>() << "\n";

                if (!row["avg_voltage"].is_null()) {
                    oss << "  Input Voltage: "
                        << row["min_voltage"].as<std::string>() << "V - "
                        << row["max_voltage"].as<std::string>() << "V (avg "
                        << row["avg_voltage"].as<std::string>() << "V)\n";
                }

                if (!row["avg_output_voltage"].is_null()) {
                    oss << "  Output Voltage: " << row["avg_output_voltage"].as<std::string>() << "V avg\n";
                }

                if (!row["avg_load_pct"].is_null()) {
                    oss << "  Load: " << row["avg_load_pct"].as<std::string>() << "% avg";
                    if (!row["avg_watts"].is_null()) {
                        oss << " (" << row["avg_watts"].as<std::string>() << "W avg, "
                            << row["max_watts"].as<std::string>() << "W peak)";
                    }
                    oss << "\n";
                }

                if (!row["avg_battery"].is_null()) {
                    oss << "  Battery: " << row["avg_battery"].as<std::string>() << "% avg, "
                        << row["min_battery"].as<std::string>() << "% min\n";
                }

                if (!row["avg_runtime_sec"].is_null()) {
                    double avg_min = row["avg_runtime_sec"].as<double>() / 60.0;
                    double min_min = row["min_runtime_sec"].as<double>() / 60.0;
                    oss << "  Runtime Reserve: " << std::fixed << std::setprecision(1)
                        << avg_min << " min avg, " << min_min << " min min\n";
                }

                if (!row["avg_temperature"].is_null()) {
                    oss << "  Temperature: " << row["avg_temperature"].as<std::string>() << "C avg\n";
                }

                int power_failures = row["power_failures"].as<int>();
                oss << "  Power Failures: " << power_failures << "\n";

                if (!row["statuses"].is_null()) {
                    oss << "  UPS Status(es): " << row["statuses"].as<std::string>() << "\n";
                }

                if (!row["transfer_reasons"].is_null()) {
                    std::string reasons = row["transfer_reasons"].as<std::string>();
                    if (!reasons.empty()) {
                        oss << "  Transfer Reasons: " << reasons << "\n";
                    }
                }

                oss << "\n";
            }

            // Also query power events for the day
            pqxx::work txn2(*conn_);
            std::string events_query =
                "SELECT d.device_name, pe.event_type, pe.event_timestamp, "
                "pe.battery_level_start, pe.battery_level_end, pe.load_at_event "
                "FROM power_events pe "
                "JOIN ups_devices d ON pe.device_id = d.device_id "
                "WHERE pe.event_timestamp::date = " + txn2.quote(date) + " "
                "ORDER BY pe.event_timestamp";

            pqxx::result events_res = txn2.exec(events_query);
            txn2.commit();

            if (!events_res.empty()) {
                oss << "Power Events:\n";
                for (const auto& ev : events_res) {
                    oss << "  - " << ev["timestamp"].as<std::string>() << ": "
                        << ev["device_name"].as<std::string>() << " - "
                        << ev["event_type"].as<std::string>();
                    if (!ev["battery_level_start"].is_null()) {
                        oss << " (battery: " << ev["battery_level_start"].as<std::string>()
                            << "% -> " << ev["battery_level_end"].as<std::string>() << "%)";
                    }
                    oss << "\n";
                }
                oss << "\n";
            }

            result = oss.str();
            return true;

        } catch (const std::exception& e) {
            std::cerr << "❌ DB: queryDailyMetrics error: " << e.what() << std::endl;
            return false;
        }
    });

    return result;
}

bool DatabaseService::logPowerEvent(int device_id,
                                     const std::string& event_type,
                                     double battery_level_start,
                                     double battery_level_end,
                                     double load_at_event) {
    return executeWithRetry([&]() -> bool {
        std::lock_guard<std::mutex> lock(connection_mutex_);

        try {
            pqxx::work txn(*conn_);

            std::ostringstream query;
            query << "INSERT INTO power_events "
                  << "(device_id, event_type, battery_level_start, battery_level_end, load_at_event) "
                  << "VALUES ("
                  << device_id << ", "
                  << txn.quote(event_type) << ", "
                  << battery_level_start << ", "
                  << battery_level_end << ", "
                  << load_at_event << ")";

            txn.exec(query.str());
            txn.commit();

            std::cout << "💾 DB: Logged power event: " << event_type
                      << " for device_id=" << device_id << std::endl;

            return true;

        } catch (const std::exception& e) {
            std::cerr << "❌ DB: logPowerEvent error: " << e.what() << std::endl;
            return false;
        }
    });
}

// ─────────────────────────────────────────────────────────────────────
// Device configuration (persisted, editable via the web UI)
// ─────────────────────────────────────────────────────────────────────

void DatabaseService::ensureDeviceConfigTable() {
    executeWithRetry([&]() -> bool {
        std::lock_guard<std::mutex> lock(connection_mutex_);
        try {
            pqxx::work txn(*conn_);
            txn.exec(
                "CREATE TABLE IF NOT EXISTS device_config ("
                "  mqtt_device_id TEXT PRIMARY KEY,"
                "  db_identifier  TEXT NOT NULL,"
                "  friendly_name  TEXT,"
                "  enabled        BOOLEAN NOT NULL DEFAULT TRUE,"
                "  created_at     TIMESTAMPTZ NOT NULL DEFAULT now(),"
                "  updated_at     TIMESTAMPTZ NOT NULL DEFAULT now()"
                ")");
            txn.commit();
            std::cout << "💾 DB: device_config table ready" << std::endl;
            return true;
        } catch (const std::exception& e) {
            std::cerr << "❌ DB: ensureDeviceConfigTable error: " << e.what() << std::endl;
            return false;
        }
    });
}

std::optional<int> DatabaseService::ensureUpsDevice(const std::string& db_identifier,
                                                    const std::string& friendly_name) {
    std::optional<int> result;
    executeWithRetry([&]() -> bool {
        std::lock_guard<std::mutex> lock(connection_mutex_);
        try {
            pqxx::work txn(*conn_);

            pqxx::result found = txn.exec(
                "SELECT device_id FROM ups_devices WHERE device_identifier = " +
                txn.quote(db_identifier));

            int device_id;
            if (!found.empty()) {
                device_id = found[0]["device_id"].as<int>();
            } else {
                pqxx::result ins = txn.exec(
                    "INSERT INTO ups_devices (device_identifier, device_name) VALUES (" +
                    txn.quote(db_identifier) + ", " + txn.quote(friendly_name) +
                    ") RETURNING device_id");
                device_id = ins[0]["device_id"].as<int>();
                std::cout << "💾 DB: created ups_devices row for " << db_identifier
                          << " (device_id=" << device_id << ")" << std::endl;
            }
            txn.commit();

            {
                std::lock_guard<std::mutex> cache_lock(cache_mutex_);
                device_id_cache_[db_identifier] = device_id;
            }
            result = device_id;
            return true;
        } catch (const std::exception& e) {
            std::cerr << "❌ DB: ensureUpsDevice error: " << e.what() << std::endl;
            return false;
        }
    });
    return result;
}

int DatabaseService::seedDeviceConfigFromEnvIfEmpty(const std::vector<DeviceConfigRow>& devices) {
    int seeded = 0;
    executeWithRetry([&]() -> bool {
        std::lock_guard<std::mutex> lock(connection_mutex_);
        try {
            pqxx::work txn(*conn_);
            pqxx::result cnt = txn.exec("SELECT COUNT(*) AS n FROM device_config");
            if (cnt[0]["n"].as<long>() > 0) {
                txn.commit();
                return true;  // already populated, do not overwrite user edits
            }
            for (const auto& d : devices) {
                txn.exec(
                    "INSERT INTO device_config (mqtt_device_id, db_identifier, friendly_name, enabled) "
                    "VALUES (" + txn.quote(d.mqtt_device_id) + ", " + txn.quote(d.db_identifier) +
                    ", " + txn.quote(d.friendly_name) + ", TRUE) "
                    "ON CONFLICT (mqtt_device_id) DO NOTHING");
                ++seeded;
            }
            txn.commit();
            if (seeded > 0)
                std::cout << "💾 DB: seeded device_config with " << seeded
                          << " device(s) from environment" << std::endl;
            return true;
        } catch (const std::exception& e) {
            std::cerr << "❌ DB: seedDeviceConfigFromEnvIfEmpty error: " << e.what() << std::endl;
            seeded = 0;
            return false;
        }
    });
    // Make sure each seeded device has an ups_devices row (best-effort).
    for (const auto& d : devices) {
        ensureUpsDevice(d.db_identifier, d.friendly_name);
    }
    return seeded;
}

std::vector<DeviceConfigRow> DatabaseService::listDeviceConfigs(bool include_disabled) {
    std::vector<DeviceConfigRow> rows;
    executeWithRetry([&]() -> bool {
        std::lock_guard<std::mutex> lock(connection_mutex_);
        try {
            pqxx::work txn(*conn_);
            std::string q =
                "SELECT mqtt_device_id, db_identifier, COALESCE(friendly_name,'') AS friendly_name, "
                "enabled FROM device_config ";
            if (!include_disabled) q += "WHERE enabled = TRUE ";
            q += "ORDER BY mqtt_device_id";
            pqxx::result res = txn.exec(q);
            txn.commit();
            for (const auto& r : res) {
                DeviceConfigRow row;
                row.mqtt_device_id = r["mqtt_device_id"].as<std::string>();
                row.db_identifier  = r["db_identifier"].as<std::string>();
                row.friendly_name  = r["friendly_name"].as<std::string>();
                row.enabled        = r["enabled"].as<bool>();
                rows.push_back(std::move(row));
            }
            return true;
        } catch (const std::exception& e) {
            std::cerr << "❌ DB: listDeviceConfigs error: " << e.what() << std::endl;
            return false;
        }
    });
    return rows;
}

bool DatabaseService::upsertDeviceConfig(const DeviceConfigRow& cfg) {
    // Guarantee the FK target exists first (also refreshes device_id cache).
    ensureUpsDevice(cfg.db_identifier, cfg.friendly_name);

    return executeWithRetry([&]() -> bool {
        std::lock_guard<std::mutex> lock(connection_mutex_);
        try {
            pqxx::work txn(*conn_);
            txn.exec(
                "INSERT INTO device_config (mqtt_device_id, db_identifier, friendly_name, enabled, updated_at) "
                "VALUES (" + txn.quote(cfg.mqtt_device_id) + ", " + txn.quote(cfg.db_identifier) +
                ", " + txn.quote(cfg.friendly_name) + ", " + (cfg.enabled ? "TRUE" : "FALSE") + ", now()) "
                "ON CONFLICT (mqtt_device_id) DO UPDATE SET "
                "db_identifier = EXCLUDED.db_identifier, "
                "friendly_name = EXCLUDED.friendly_name, "
                "enabled = EXCLUDED.enabled, "
                "updated_at = now()");
            txn.commit();
            std::cout << "💾 DB: upserted device_config " << cfg.mqtt_device_id << std::endl;
            return true;
        } catch (const std::exception& e) {
            std::cerr << "❌ DB: upsertDeviceConfig error: " << e.what() << std::endl;
            return false;
        }
    });
}

bool DatabaseService::setDeviceEnabled(const std::string& mqtt_device_id, bool enabled) {
    return executeWithRetry([&]() -> bool {
        std::lock_guard<std::mutex> lock(connection_mutex_);
        try {
            pqxx::work txn(*conn_);
            txn.exec("UPDATE device_config SET enabled = " + std::string(enabled ? "TRUE" : "FALSE") +
                     ", updated_at = now() WHERE mqtt_device_id = " + txn.quote(mqtt_device_id));
            txn.commit();
            return true;
        } catch (const std::exception& e) {
            std::cerr << "❌ DB: setDeviceEnabled error: " << e.what() << std::endl;
            return false;
        }
    });
}

bool DatabaseService::deleteDeviceConfig(const std::string& mqtt_device_id) {
    return executeWithRetry([&]() -> bool {
        std::lock_guard<std::mutex> lock(connection_mutex_);
        try {
            pqxx::work txn(*conn_);
            txn.exec("DELETE FROM device_config WHERE mqtt_device_id = " + txn.quote(mqtt_device_id));
            txn.commit();
            std::cout << "💾 DB: deleted device_config " << mqtt_device_id
                      << " (ups_devices history retained)" << std::endl;
            return true;
        } catch (const std::exception& e) {
            std::cerr << "❌ DB: deleteDeviceConfig error: " << e.what() << std::endl;
            return false;
        }
    });
}

Json::Value DatabaseService::queryHistory(const std::string& db_identifier, int hours) {
    if (hours < 1) hours = 1;
    if (hours > 24 * 90) hours = 24 * 90;   // clamp to 90 days
    Json::Value arr(Json::arrayValue);
    executeWithRetry([&]() -> bool {
        std::lock_guard<std::mutex> lock(connection_mutex_);
        try {
            pqxx::work txn(*conn_);
            std::string q =
                "SELECT to_char(m.timestamp, 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS t, "
                "m.battery_charge, m.load_percentage, m.input_voltage "
                "FROM ups_metrics m JOIN ups_devices d ON m.device_id = d.device_id "
                "WHERE d.device_identifier = " + txn.quote(db_identifier) + " "
                "AND m.timestamp > now() - INTERVAL '" + std::to_string(hours) + " hours' "
                "ORDER BY m.timestamp";
            pqxx::result res = txn.exec(q);
            txn.commit();
            for (const auto& r : res) {
                Json::Value pt;
                pt["t"] = r["t"].as<std::string>();
                pt["battery_charge"]  = r["battery_charge"].is_null()  ? Json::Value(Json::nullValue) : Json::Value(r["battery_charge"].as<double>());
                pt["load_percentage"] = r["load_percentage"].is_null() ? Json::Value(Json::nullValue) : Json::Value(r["load_percentage"].as<double>());
                pt["input_voltage"]   = r["input_voltage"].is_null()   ? Json::Value(Json::nullValue) : Json::Value(r["input_voltage"].as<double>());
                arr.append(pt);
            }
            return true;
        } catch (const std::exception& e) {
            std::cerr << "❌ DB: queryHistory error: " << e.what() << std::endl;
            return false;
        }
    });
    return arr;
}

Json::Value DatabaseService::queryRecentEvents(const std::string& db_identifier, int limit) {
    if (limit < 1) limit = 1;
    if (limit > 500) limit = 500;
    Json::Value arr(Json::arrayValue);
    executeWithRetry([&]() -> bool {
        std::lock_guard<std::mutex> lock(connection_mutex_);
        try {
            pqxx::work txn(*conn_);
            std::string q =
                "SELECT d.device_name, d.device_identifier, pe.event_type, "
                "to_char(pe.event_timestamp, 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS ts, "
                "pe.battery_level_start, pe.battery_level_end, pe.load_at_event "
                "FROM power_events pe JOIN ups_devices d ON pe.device_id = d.device_id ";
            if (!db_identifier.empty())
                q += "WHERE d.device_identifier = " + txn.quote(db_identifier) + " ";
            q += "ORDER BY pe.event_timestamp DESC LIMIT " + std::to_string(limit);
            pqxx::result res = txn.exec(q);
            txn.commit();
            for (const auto& r : res) {
                Json::Value ev;
                ev["device_name"]     = r["device_name"].as<std::string>();
                ev["device_identifier"] = r["device_identifier"].as<std::string>();
                ev["event_type"]      = r["event_type"].as<std::string>();
                ev["timestamp"]       = r["ts"].as<std::string>();
                ev["battery_start"]   = r["battery_level_start"].is_null() ? Json::Value(Json::nullValue) : Json::Value(r["battery_level_start"].as<double>());
                ev["battery_end"]     = r["battery_level_end"].is_null()   ? Json::Value(Json::nullValue) : Json::Value(r["battery_level_end"].as<double>());
                ev["load"]            = r["load_at_event"].is_null()       ? Json::Value(Json::nullValue) : Json::Value(r["load_at_event"].as<double>());
                arr.append(ev);
            }
            return true;
        } catch (const std::exception& e) {
            std::cerr << "❌ DB: queryRecentEvents error: " << e.what() << std::endl;
            return false;
        }
    });
    return arr;
}

}  // namespace hms_nut
