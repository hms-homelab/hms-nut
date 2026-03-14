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
#include <iostream>
#include <memory>
#include <chrono>
#include <iomanip>

using namespace hms_nut;

// Global services for signal handler
std::unique_ptr<NutBridgeService> g_nut_bridge;
std::unique_ptr<CollectorService> g_collector;
std::unique_ptr<DailySummaryService> g_daily_summary;
std::shared_ptr<MqttClient> g_mqtt_client;

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

        // Create and start NUT Bridge Service
        // Service will handle connection failures and retry with exponential backoff
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
        g_nut_bridge->setupSubscriptions();
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
                response["version"] = "1.0";

                // Check component status
                Json::Value components;
                components["mqtt"] = g_mqtt_client && g_mqtt_client->isConnected() ? "connected" : "disconnected";
                components["database"] = DatabaseService::getInstance().isConnected() ? "connected" : "disconnected";
                components["nut_bridge"] = g_nut_bridge && g_nut_bridge->isRunning() ? "running" : "stopped";
                components["collector"] = g_collector && g_collector->isRunning() ? "running" : "stopped";
                components["daily_summary"] = g_daily_summary && g_daily_summary->isRunning() ? "running" : "disabled";

                // Overall status
                bool all_ok = (g_mqtt_client && g_mqtt_client->isConnected()) &&
                              DatabaseService::getInstance().isConnected() &&
                              (g_nut_bridge && g_nut_bridge->isRunning()) &&
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

        // Configure Drogon
        drogon::app().addListener("0.0.0.0", health_check_port);
        drogon::app().setThreadNum(1);  // Minimal threads for health check
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
