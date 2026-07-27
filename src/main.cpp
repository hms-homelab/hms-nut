#include "services/NutBridgeService.h"
#include "services/CollectorService.h"
#include "services/DailySummaryService.h"
#include "mqtt/MqttClient.h"
#include "database/DatabaseService.h"
#include "utils/DeviceMapper.h"
#include "llm_client.h"
#include <drogon/drogon.h>
#include <csignal>
#include <cstdlib>
#include <cctype>
#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>
#include <chrono>
#include <iomanip>
#include <vector>

using namespace hms_nut;

#define HMS_NUT_VERSION "1.4.0"

// Global services for signal handler
std::unique_ptr<NutBridgeService> g_nut_bridge;
std::unique_ptr<CollectorService> g_collector;
std::unique_ptr<DailySummaryService> g_daily_summary;
std::shared_ptr<MqttClient> g_mqtt_client;

// Reload the DeviceMapper from the persisted device_config and reconcile the
// collector's MQTT subscriptions. Called after any device-management edit so
// changes take effect live, without a service restart.
static void reloadDeviceConfigAndSubs() {
    auto& db = DatabaseService::getInstance();
    auto rows = db.listDeviceConfigs(false);  // enabled only
    std::vector<DeviceConfig> cfgs;
    for (const auto& r : rows) {
        DeviceConfig c;
        c.mqtt_device_id = r.mqtt_device_id;
        c.db_identifier  = r.db_identifier;
        c.friendly_name  = r.friendly_name;
        cfgs.push_back(std::move(c));
    }
    DeviceMapper::loadFromConfigs(cfgs);
    if (g_collector) {
        g_collector->reloadSubscriptions();
    }
}

// Validate an MQTT-safe device id (topic segment): [A-Za-z0-9_-], non-empty.
static bool isValidDeviceId(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (char c : id) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

// Split a comma-separated device list, trimming blanks and dropping invalid ids.
static std::vector<std::string> splitCsv(const std::string& csv) {
    std::vector<std::string> out;
    std::istringstream iss(csv);
    std::string item;
    while (std::getline(iss, item, ',')) {
        // trim surrounding whitespace
        size_t b = item.find_first_not_of(" \t");
        size_t e = item.find_last_not_of(" \t");
        if (b == std::string::npos) continue;
        std::string id = item.substr(b, e - b + 1);
        if (isValidDeviceId(id)) out.push_back(id);
    }
    return out;
}

// Send a Json::Value as an application/json response with a status code.
static void sendJson(std::function<void(const drogon::HttpResponsePtr&)>& cb,
                     const Json::Value& body, drogon::HttpStatusCode code = drogon::k200OK) {
    Json::StreamWriterBuilder writer;
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(code);
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    resp->setBody(Json::writeString(writer, body));
    cb(resp);
}

void signalHandler(int signal) {
    std::cout << "\n🛑 Received signal " << signal << ", shutting down gracefully..." << std::endl;

    // Stop services
    if (g_daily_summary) {
        g_daily_summary->stop();
    }
    if (g_collector) {
        g_collector->stop();
    }
    if (g_nut_bridge) {
        g_nut_bridge->stop();
    }

    // Disconnect MQTT
    if (g_mqtt_client) {
        g_mqtt_client->disconnect();
    }

    // Close database
    DatabaseService::getInstance().close();

    std::cout << "✅ Shutdown complete" << std::endl;
    std::exit(0);
}

// Helper to get environment variable with default
std::string getEnv(const char* name, const std::string& default_value = "") {
    const char* value = std::getenv(name);
    return value ? std::string(value) : default_value;
}

int getEnvInt(const char* name, int default_value = 0) {
    const char* value = std::getenv(name);
    return value ? std::atoi(value) : default_value;
}

int main() {
    std::cout << R"(
╔════════════════════════════════════════╗
║       HMS-NUT v1.0                     ║
║   Unified UPS Monitoring Service       ║
║   (C++ Native Implementation)          ║
╚════════════════════════════════════════╝
)" << std::endl;

    // Install signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Read configuration from environment variables
    bool nut_enabled = getEnv("NUT_ENABLED", "true") == "true";
    std::string nut_host = getEnv("NUT_HOST", "localhost");
    int nut_port = getEnvInt("NUT_PORT", 3493);
    std::string nut_ups_name = getEnv("NUT_UPS_NAME", "apc_bx@localhost");
    std::string nut_device_id = getEnv("NUT_DEVICE_ID", "apc_ups");
    std::string nut_device_name = getEnv("NUT_DEVICE_NAME", "Docker NUT UPS");
    int nut_poll_interval = getEnvInt("NUT_POLL_INTERVAL", 60);

    std::string mqtt_broker = getEnv("MQTT_BROKER", "localhost");
    int mqtt_port = getEnvInt("MQTT_PORT", 1883);
    std::string mqtt_user = getEnv("MQTT_USER", "");
    std::string mqtt_password = getEnv("MQTT_PASSWORD", "");
    std::string mqtt_client_id = getEnv("MQTT_CLIENT_ID", "hms_nut_service");

    std::string db_host = getEnv("DB_HOST", "localhost");
    int db_port = getEnvInt("DB_PORT", 5432);
    std::string db_name = getEnv("DB_NAME", "ups_monitoring");
    std::string db_user = getEnv("DB_USER", "");
    std::string db_password = getEnv("DB_PASSWORD", "");

    int collector_save_interval = getEnvInt("COLLECTOR_SAVE_INTERVAL", 3600);
    int health_check_port = getEnvInt("HEALTH_CHECK_PORT", 8892);  // Changed from 8891 (used by hms-weather)

    // LLM configuration
    bool llm_enabled = getEnv("LLM_ENABLED", "false") == "true";
    std::string llm_provider_str = getEnv("LLM_PROVIDER", "ollama");
    std::string llm_endpoint = getEnv("LLM_ENDPOINT", "http://192.168.2.5:11434");
    std::string llm_model = getEnv("LLM_MODEL", "llama3.1:8b-instruct-q4_K_M");
    std::string llm_api_key = getEnv("LLM_API_KEY", "");
    std::string llm_prompt_file = getEnv("LLM_PROMPT_FILE", "llm_prompt.txt");
    int summary_hour = getEnvInt("SUMMARY_HOUR", 7);

    std::cout << "⚙️  Configuration:" << std::endl;
    std::cout << "   NUT Server: " << nut_host << ":" << nut_port << std::endl;
    std::cout << "   UPS Name: " << nut_ups_name << std::endl;
    std::cout << "   Device ID: " << nut_device_id << std::endl;
    std::cout << "   Poll Interval: " << nut_poll_interval << "s" << std::endl;
    std::cout << "   MQTT Broker: tcp://" << mqtt_broker << ":" << mqtt_port << std::endl;
    std::cout << "   Database: " << db_name << "@" << db_host << ":" << db_port << std::endl;
    std::cout << "   Collector Save Interval: " << collector_save_interval << "s" << std::endl;
    std::cout << "   Health Check Port: " << health_check_port << std::endl;
    std::cout << "   LLM Enabled: " << (llm_enabled ? "true" : "false") << std::endl;
    if (llm_enabled) {
        std::cout << "   LLM Provider: " << llm_provider_str << std::endl;
        std::cout << "   LLM Model: " << llm_model << std::endl;
        std::cout << "   LLM Endpoint: " << llm_endpoint << std::endl;
        std::cout << "   Summary Hour: " << summary_hour << ":00" << std::endl;
        std::cout << "   Prompt File: " << llm_prompt_file << std::endl;
    }
    std::cout << std::endl;

    // Initialize device mapper from environment
    // This reads UPS_DEVICE_IDS, UPS_DB_MAPPING, UPS_FRIENDLY_NAMES
    // Falls back to NUT_DEVICE_ID if UPS_DEVICE_IDS not set
    DeviceMapper::initialize();
    std::cout << std::endl;

    try {
        // Initialize MQTT client (non-blocking)
        std::cout << "🚀 Initializing MQTT client..." << std::endl;
        g_mqtt_client = std::make_shared<MqttClient>(mqtt_client_id);

        std::string mqtt_broker_url = "tcp://" + mqtt_broker + ":" + std::to_string(mqtt_port);
        if (!g_mqtt_client->connect(mqtt_broker_url, mqtt_user, mqtt_password)) {
            std::cerr << "⚠️  Initial MQTT connection failed - services will retry automatically" << std::endl;
            // Don't exit - services will handle reconnection with exponential backoff
        }

        // Initialize database (non-blocking)
        std::cout << "🚀 Initializing database..." << std::endl;
        std::string db_connection = "host=" + db_host +
                                    " port=" + std::to_string(db_port) +
                                    " dbname=" + db_name +
                                    " user=" + db_user +
                                    " password=" + db_password;
        DatabaseService::getInstance().initialize(db_connection);

        if (!DatabaseService::getInstance().isConnected()) {
            std::cerr << "⚠️  Initial database connection failed - will retry on first operation" << std::endl;
            // Don't exit - DatabaseService has built-in retry logic
        }

        // ── Device configuration: seed from env on first run, then DB is authoritative ──
        // DeviceMapper::initialize() already read UPS_* env into memory above. Persist
        // that as the initial device_config (only if the table is empty), then reload the
        // mapper from the DB so runtime edits via the web UI take effect. If the DB is
        // unavailable, listDeviceConfigs() returns empty and we keep the env-based config.
        {
            auto& db = DatabaseService::getInstance();
            db.ensureDeviceConfigTable();
            db.ensureDailySummaryTable();

            std::vector<DeviceConfigRow> seed;
            for (const auto& mqtt_id : DeviceMapper::getDeviceIds()) {
                DeviceConfigRow r;
                r.mqtt_device_id = mqtt_id;
                r.db_identifier  = DeviceMapper::getDbIdentifier(mqtt_id);
                r.friendly_name  = DeviceMapper::getFriendlyName(mqtt_id);
                r.enabled        = true;
                seed.push_back(std::move(r));
            }
            db.seedDeviceConfigFromEnvIfEmpty(seed);

            auto rows = db.listDeviceConfigs(false);  // enabled only
            if (!rows.empty()) {
                std::vector<DeviceConfig> cfgs;
                for (const auto& r : rows) {
                    DeviceConfig c;
                    c.mqtt_device_id = r.mqtt_device_id;
                    c.db_identifier  = r.db_identifier;
                    c.friendly_name  = r.friendly_name;
                    cfgs.push_back(std::move(c));
                }
                DeviceMapper::loadFromConfigs(cfgs);
            } else {
                std::cerr << "⚠️  device_config empty/unavailable - keeping env-based device list"
                          << std::endl;
            }
        }

        // Create and start NUT Bridge Service (optional — set NUT_ENABLED=false when
        // there is no local NUT-attached UPS, e.g. after retiring the USB UPS).
        if (nut_enabled) {
            std::cout << "🚀 Starting NUT Bridge Service..." << std::endl;
            g_nut_bridge = std::make_unique<NutBridgeService>(
                g_mqtt_client,
                nut_host,
                nut_port,
                nut_ups_name,
                nut_device_id,
                nut_device_name,
                nut_poll_interval
            );
            g_nut_bridge->start();
        } else {
            std::cout << "⏭️  NUT Bridge disabled (NUT_ENABLED=false) — MQTT-only mode" << std::endl;
        }

        // Create and start Collector Service
        // Will subscribe once MQTT connection is available
        std::cout << "🚀 Starting Collector Service..." << std::endl;
        g_collector = std::make_unique<CollectorService>(
            g_mqtt_client,
            DatabaseService::getInstance(),
            collector_save_interval
        );
        g_collector->start();

        // Create and start Daily Summary Service (LLM-powered)
        hms::LLMConfig llm_config;
        llm_config.enabled = llm_enabled;
        llm_config.provider = hms::LLMClient::parseProvider(llm_provider_str);
        llm_config.endpoint = llm_endpoint;
        llm_config.model = llm_model;
        llm_config.api_key = llm_api_key;
        llm_config.keep_alive_seconds = 0;  // Evict model from VRAM after call

        std::cout << "🚀 Starting Daily Summary Service..." << std::endl;
        g_daily_summary = std::make_unique<DailySummaryService>(
            g_mqtt_client,
            DatabaseService::getInstance(),
            llm_config,
            summary_hour,
            llm_prompt_file
        );
        g_daily_summary->start();

        // Setup MQTT subscriptions (following HMS-FireTV pattern)
        // This is done AFTER starting services but BEFORE starting Drogon
        // Subscriptions may block waiting for MQTT connection, but that's OK here
        std::cout << "🚀 Setting up MQTT subscriptions..." << std::endl;
        if (g_nut_bridge) {
            g_nut_bridge->setupSubscriptions();
        }
        g_collector->setupSubscriptions();
        std::cout << "✅ MQTT subscriptions configured" << std::endl;

        // Publish HA discovery for daily summary sensor
        if (llm_enabled) {
            g_daily_summary->publishDiscovery();
        }

        // Setup health check endpoint
        drogon::app().registerHandler(
            "/health",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

                Json::Value response;
                response["service"] = "hms-nut";
                response["version"] = HMS_NUT_VERSION;

                // Check component status
                Json::Value components;
                components["mqtt"] = g_mqtt_client && g_mqtt_client->isConnected() ? "connected" : "disconnected";
                components["database"] = DatabaseService::getInstance().isConnected() ? "connected" : "disconnected";
                components["nut_bridge"] = g_nut_bridge ? (g_nut_bridge->isRunning() ? "running" : "stopped") : "disabled";
                components["collector"] = g_collector && g_collector->isRunning() ? "running" : "stopped";
                components["daily_summary"] = g_daily_summary && g_daily_summary->isRunning() ? "running" : "disabled";

                // Overall status — the NUT bridge is optional, so only require it when enabled
                bool all_ok = (g_mqtt_client && g_mqtt_client->isConnected()) &&
                              DatabaseService::getInstance().isConnected() &&
                              (!g_nut_bridge || g_nut_bridge->isRunning()) &&
                              (g_collector && g_collector->isRunning());

                response["status"] = all_ok ? "healthy" : "degraded";
                response["components"] = components;

                // Timestamps
                if (g_nut_bridge) {
                    auto last_poll = g_nut_bridge->getLastPollTime();
                    auto time_t_val = std::chrono::system_clock::to_time_t(last_poll);
                    std::ostringstream oss;
                    oss << std::put_time(std::gmtime(&time_t_val), "%Y-%m-%dT%H:%M:%SZ");
                    response["last_nut_poll"] = oss.str();
                }

                if (g_collector) {
                    auto last_save = g_collector->getLastSaveTime();
                    auto time_t_val = std::chrono::system_clock::to_time_t(last_save);
                    std::ostringstream oss;
                    oss << std::put_time(std::gmtime(&time_t_val), "%Y-%m-%dT%H:%M:%SZ");
                    response["last_db_save"] = oss.str();
                    response["devices_monitored"] = g_collector->getDeviceCount();
                }

                if (g_daily_summary) {
                    auto last_summary = g_daily_summary->getLastSummaryTime();
                    if (last_summary != std::chrono::system_clock::time_point{}) {
                        auto time_t_val = std::chrono::system_clock::to_time_t(last_summary);
                        std::ostringstream oss;
                        oss << std::put_time(std::gmtime(&time_t_val), "%Y-%m-%dT%H:%M:%SZ");
                        response["last_daily_summary"] = oss.str();
                    }
                }

                // Serialize response
                Json::StreamWriterBuilder writer;
                std::string json_str = Json::writeString(writer, response);

                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(all_ok ? drogon::k200OK : drogon::k503ServiceUnavailable);
                resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
                resp->setBody(json_str);

                callback(resp);
            },
            {drogon::Get}
        );

        // Setup republish endpoint
        drogon::app().registerHandler(
            "/republish",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

                Json::Value response;
                response["service"] = "hms-nut";

                if (!g_nut_bridge) {
                    response["success"] = false;
                    response["message"] = "NUT bridge not initialized";

                    Json::StreamWriterBuilder writer;
                    std::string json_str = Json::writeString(writer, response);

                    auto resp = drogon::HttpResponse::newHttpResponse();
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
                    resp->setBody(json_str);
                    callback(resp);
                    return;
                }

                bool result = g_nut_bridge->republishDiscovery();
                response["success"] = result;
                response["message"] = result ? "Discovery messages republished successfully" : "Failed to republish discovery messages";

                Json::StreamWriterBuilder writer;
                std::string json_str = Json::writeString(writer, response);

                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(result ? drogon::k200OK : drogon::k500InternalServerError);
                resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
                resp->setBody(json_str);

                callback(resp);
            },
            {drogon::Post}
        );

        // Setup manual summary trigger endpoint
        // POST /summary?date=2026-03-13  (defaults to yesterday)
        drogon::app().registerHandler(
            "/summary",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

                Json::Value response;
                response["service"] = "hms-nut";

                if (!g_daily_summary || !g_daily_summary->isRunning()) {
                    response["success"] = false;
                    response["message"] = "Daily summary service not running";

                    Json::StreamWriterBuilder writer;
                    auto resp = drogon::HttpResponse::newHttpResponse();
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
                    resp->setBody(Json::writeString(writer, response));
                    callback(resp);
                    return;
                }

                // Get date parameter (default: yesterday)
                std::string date = req->getParameter("date");
                if (date.empty()) {
                    auto yesterday = std::chrono::system_clock::now() - std::chrono::hours(24);
                    auto yesterday_t = std::chrono::system_clock::to_time_t(yesterday);
                    std::tm yesterday_tm;
                    localtime_r(&yesterday_t, &yesterday_tm);
                    std::ostringstream oss;
                    oss << std::put_time(&yesterday_tm, "%Y-%m-%d");
                    date = oss.str();
                }

                bool result = g_daily_summary->generateSummary(date);
                response["success"] = result;
                response["date"] = date;
                if (result) {
                    response["summary"] = g_daily_summary->getLastSummary();
                } else {
                    response["message"] = "Summary generation failed";
                }

                Json::StreamWriterBuilder writer;
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(result ? drogon::k200OK : drogon::k500InternalServerError);
                resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
                resp->setBody(Json::writeString(writer, response));
                callback(resp);
            },
            {drogon::Post}
        );

        // ─────────────────────────────────────────────────────────────
        // Web UI REST API
        // ─────────────────────────────────────────────────────────────

        // GET /api/devices — configured devices with live status
        drogon::app().registerHandler(
            "/api/devices",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                Json::Value out(Json::arrayValue);
                std::map<std::string, UpsData> snap;
                if (g_collector) snap = g_collector->snapshotData();

                for (const auto& mqtt_id : DeviceMapper::getDeviceIds()) {
                    std::string db_id = DeviceMapper::getDbIdentifier(mqtt_id);
                    Json::Value d;
                    d["mqtt_device_id"] = mqtt_id;
                    d["db_identifier"]  = db_id;
                    d["friendly_name"]  = DeviceMapper::getFriendlyName(mqtt_id);

                    auto it = snap.find(db_id);
                    if (it != snap.end()) {
                        d["online"] = true;
                        Json::Value metrics;
                        Json::CharReaderBuilder rb;
                        std::string errs;
                        std::string js = it->second.toJson();
                        std::istringstream iss(js);
                        if (Json::parseFromStream(rb, iss, &metrics, &errs)) {
                            d["metrics"] = metrics;
                        } else {
                            d["metrics"] = Json::Value(Json::nullValue);
                        }
                    } else {
                        d["online"] = false;
                        d["metrics"] = Json::Value(Json::nullValue);
                    }
                    out.append(d);
                }
                sendJson(callback, out);
            },
            {drogon::Get});

        // GET /api/history?device=<mqtt_id>[,<mqtt_id>…]&hours=N
        //
        // Accepts a comma-separated device list so the UI can overlay several
        // nodes on one chart in a single round trip. The response always carries
        // `series` (one entry per requested device); `device`/`points` mirror the
        // first series so existing single-device callers keep working.
        drogon::app().registerHandler(
            "/api/history",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                std::string device = req->getParameter("device");
                if (device.empty()) {
                    Json::Value err; err["error"] = "missing 'device' parameter";
                    sendJson(callback, err, drogon::k400BadRequest);
                    return;
                }

                std::vector<std::string> devices = splitCsv(device);
                if (devices.empty()) {
                    Json::Value err; err["error"] = "no valid device ids in 'device'";
                    sendJson(callback, err, drogon::k400BadRequest);
                    return;
                }
                // Bound the fan-out — one DB round trip per device.
                if (devices.size() > 16) devices.resize(16);

                int hours = 24;
                std::string h = req->getParameter("hours");
                if (!h.empty()) hours = std::atoi(h.c_str());

                auto& db = DatabaseService::getInstance();
                Json::Value series(Json::arrayValue);
                for (const auto& mqtt_id : devices) {
                    std::string db_id = DeviceMapper::getDbIdentifier(mqtt_id);
                    Json::Value s;
                    s["device"]        = mqtt_id;
                    s["db_identifier"] = db_id;
                    s["friendly_name"] = DeviceMapper::getFriendlyName(mqtt_id);
                    s["points"]        = db.queryHistory(db_id, hours);
                    series.append(s);
                }

                Json::Value out;
                out["hours"]  = hours;
                out["series"] = series;
                out["device"] = series[0]["device"];   // back-compat
                out["points"] = series[0]["points"];   // back-compat
                sendJson(callback, out);
            },
            {drogon::Get});

        // GET /api/summaries?limit=N — persisted daily energy summaries, newest first.
        // The live in-memory summary is folded in when it is newer than what the DB
        // holds (e.g. the DB write failed, or LLM ran before the table existed).
        drogon::app().registerHandler(
            "/api/summaries",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                int limit = 14;
                std::string l = req->getParameter("limit");
                if (!l.empty()) limit = std::atoi(l.c_str());

                Json::Value out;
                out["enabled"] = g_daily_summary && g_daily_summary->isRunning();
                Json::Value stored = DatabaseService::getInstance().queryDailySummaries(limit);

                if (g_daily_summary) {
                    std::string live_date = g_daily_summary->getLastSummaryDate();
                    std::string live_text = g_daily_summary->getLastSummary();
                    if (!live_date.empty() && !live_text.empty()) {
                        bool present = false;
                        for (const auto& s : stored) {
                            if (s["date"].asString() == live_date) { present = true; break; }
                        }
                        if (!present) {
                            Json::Value s;
                            s["date"] = live_date;
                            s["summary"] = live_text;
                            s["model"] = "";
                            s["generated_at"] = "";
                            // Newest first — the live one covers the most recent run.
                            Json::Value merged(Json::arrayValue);
                            merged.append(s);
                            for (const auto& e : stored) merged.append(e);
                            stored = merged;
                        }
                    }
                }

                out["summaries"] = stored;
                sendJson(callback, out);
            },
            {drogon::Get});

        // POST /api/summary?date=YYYY-MM-DD — generate on demand from the UI.
        // Mirrors POST /summary but lives under /api so it shares the SPA proxy.
        drogon::app().registerHandler(
            "/api/summary",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                Json::Value out;
                if (!g_daily_summary || !g_daily_summary->isRunning()) {
                    out["success"] = false;
                    out["message"] = "daily summary service not running (LLM_ENABLED=false?)";
                    sendJson(callback, out, drogon::k503ServiceUnavailable);
                    return;
                }

                std::string date = req->getParameter("date");
                if (date.empty()) {
                    auto yesterday = std::chrono::system_clock::now() - std::chrono::hours(24);
                    auto yesterday_t = std::chrono::system_clock::to_time_t(yesterday);
                    std::tm yesterday_tm;
                    localtime_r(&yesterday_t, &yesterday_tm);
                    std::ostringstream oss;
                    oss << std::put_time(&yesterday_tm, "%Y-%m-%d");
                    date = oss.str();
                }

                bool ok = g_daily_summary->generateSummary(date);
                out["success"] = ok;
                out["date"] = date;
                if (ok) {
                    out["summary"] = g_daily_summary->getLastSummary();
                } else {
                    out["message"] = "summary generation failed (no metrics for that date, or LLM error)";
                }
                sendJson(callback, out, ok ? drogon::k200OK : drogon::k500InternalServerError);
            },
            {drogon::Post});

        // GET /api/events?device=<mqtt_id>&limit=N   (device optional = all)
        drogon::app().registerHandler(
            "/api/events",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                std::string device = req->getParameter("device");
                int limit = 50;
                std::string l = req->getParameter("limit");
                if (!l.empty()) limit = std::atoi(l.c_str());
                std::string db_id = device.empty() ? "" : DeviceMapper::getDbIdentifier(device);
                sendJson(callback, DatabaseService::getInstance().queryRecentEvents(db_id, limit));
            },
            {drogon::Get});

        // GET/POST /api/config/devices — list all / add-or-update
        drogon::app().registerHandler(
            "/api/config/devices",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                auto& db = DatabaseService::getInstance();

                if (req->method() == drogon::Get) {
                    Json::Value out(Json::arrayValue);
                    for (const auto& r : db.listDeviceConfigs(true)) {
                        Json::Value d;
                        d["mqtt_device_id"] = r.mqtt_device_id;
                        d["db_identifier"]  = r.db_identifier;
                        d["friendly_name"]  = r.friendly_name;
                        d["enabled"]        = r.enabled;
                        out.append(d);
                    }
                    sendJson(callback, out);
                    return;
                }

                // POST — add or update
                auto json = req->getJsonObject();
                if (!json) {
                    Json::Value err; err["error"] = "invalid JSON body";
                    sendJson(callback, err, drogon::k400BadRequest);
                    return;
                }
                DeviceConfigRow row;
                row.mqtt_device_id = (*json)["mqtt_device_id"].asString();
                if (!isValidDeviceId(row.mqtt_device_id)) {
                    Json::Value err; err["error"] = "mqtt_device_id must be [A-Za-z0-9_-], 1-64 chars";
                    sendJson(callback, err, drogon::k400BadRequest);
                    return;
                }
                row.db_identifier = (*json).get("db_identifier", row.mqtt_device_id).asString();
                if (row.db_identifier.empty()) row.db_identifier = row.mqtt_device_id;
                row.friendly_name = (*json).get("friendly_name", "").asString();
                row.enabled = (*json).get("enabled", true).asBool();

                if (!db.upsertDeviceConfig(row)) {
                    Json::Value err; err["error"] = "database write failed";
                    sendJson(callback, err, drogon::k500InternalServerError);
                    return;
                }
                reloadDeviceConfigAndSubs();

                Json::Value out;
                out["mqtt_device_id"] = row.mqtt_device_id;
                out["db_identifier"]  = row.db_identifier;
                out["friendly_name"]  = row.friendly_name;
                out["enabled"]        = row.enabled;
                sendJson(callback, out);
            },
            {drogon::Get, drogon::Post});

        // PUT/DELETE /api/config/devices/{id}
        drogon::app().registerHandler(
            "/api/config/devices/{id}",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
                auto& db = DatabaseService::getInstance();
                if (!isValidDeviceId(id)) {
                    Json::Value err; err["error"] = "invalid device id";
                    sendJson(callback, err, drogon::k400BadRequest);
                    return;
                }

                if (req->method() == drogon::Delete) {
                    bool ok = db.deleteDeviceConfig(id);
                    reloadDeviceConfigAndSubs();
                    Json::Value out; out["deleted"] = ok; out["mqtt_device_id"] = id;
                    sendJson(callback, out, ok ? drogon::k200OK : drogon::k500InternalServerError);
                    return;
                }

                // PUT — update fields
                auto json = req->getJsonObject();
                if (!json) {
                    Json::Value err; err["error"] = "invalid JSON body";
                    sendJson(callback, err, drogon::k400BadRequest);
                    return;
                }
                DeviceConfigRow row;
                row.mqtt_device_id = id;
                row.db_identifier  = (*json).get("db_identifier", id).asString();
                if (row.db_identifier.empty()) row.db_identifier = id;
                row.friendly_name  = (*json).get("friendly_name", "").asString();
                row.enabled        = (*json).get("enabled", true).asBool();

                if (!db.upsertDeviceConfig(row)) {
                    Json::Value err; err["error"] = "database write failed";
                    sendJson(callback, err, drogon::k500InternalServerError);
                    return;
                }
                reloadDeviceConfigAndSubs();

                Json::Value out;
                out["mqtt_device_id"] = row.mqtt_device_id;
                out["db_identifier"]  = row.db_identifier;
                out["friendly_name"]  = row.friendly_name;
                out["enabled"]        = row.enabled;
                sendJson(callback, out);
            },
            {drogon::Put, drogon::Delete});

        // ── SPA static hosting (Angular 21 build copied to STATIC_DIR) ──
        std::string static_dir = getEnv("WEB_STATIC_DIR", "./static");
        {
            std::string index_path = static_dir + "/index.html";
            std::ifstream ifs(index_path);
            if (ifs) {
                std::string index_html((std::istreambuf_iterator<char>(ifs)),
                                       std::istreambuf_iterator<char>());
                drogon::app().setCustomErrorHandler(
                    [index_html](drogon::HttpStatusCode code) -> drogon::HttpResponsePtr {
                        if (code == drogon::k404NotFound) {
                            auto resp = drogon::HttpResponse::newHttpResponse();
                            resp->setContentTypeCode(drogon::CT_TEXT_HTML);
                            resp->setBody(index_html);
                            return resp;
                        }
                        auto resp = drogon::HttpResponse::newHttpResponse();
                        resp->setStatusCode(code);
                        return resp;
                    });
                drogon::app().setDocumentRoot(static_dir).setStaticFilesCacheTime(3600);
                std::cout << "🌐 Web UI static root: " << static_dir << std::endl;
            } else {
                std::cout << "ℹ️  No web UI build at " << static_dir
                          << " (API still served; run the Angular build to enable the UI)" << std::endl;
            }
        }

        // Configure Drogon
        drogon::app().addListener("0.0.0.0", health_check_port);
        drogon::app().setThreadNum(2);  // health/API + UI
        drogon::app().setLogLevel(trantor::Logger::kWarn);  // Reduce verbosity

        std::cout << "✅ HMS-NUT started successfully" << std::endl;
        std::cout << "   Health check: http://localhost:" << health_check_port << "/health" << std::endl;
        std::cout << "   Press Ctrl+C to stop" << std::endl;
        std::cout << std::endl;

        // Run Drogon event loop (blocks)
        drogon::app().run();

    } catch (const std::exception& e) {
        std::cerr << "❌ Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
