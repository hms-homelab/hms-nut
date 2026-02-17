#include "services/NutBridgeService.h"
#include "services/CollectorService.h"
#include "mqtt/MqttClient.h"
#include "database/DatabaseService.h"
#include "utils/DeviceMapper.h"
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
std::shared_ptr<MqttClient> g_mqtt_client;

void signalHandler(int signal) {
    std::cout << "\n🛑 Received signal " << signal << ", shutting down gracefully..." << std::endl;

    // Stop services
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

    std::cout << "⚙️  Configuration:" << std::endl;
    std::cout << "   NUT Server: " << nut_host << ":" << nut_port << std::endl;
    std::cout << "   UPS Name: " << nut_ups_name << std::endl;
    std::cout << "   Device ID: " << nut_device_id << std::endl;
    std::cout << "   Poll Interval: " << nut_poll_interval << "s" << std::endl;
    std::cout << "   MQTT Broker: tcp://" << mqtt_broker << ":" << mqtt_port << std::endl;
    std::cout << "   Database: " << db_name << "@" << db_host << ":" << db_port << std::endl;
    std::cout << "   Collector Save Interval: " << collector_save_interval << "s" << std::endl;
    std::cout << "   Health Check Port: " << health_check_port << std::endl;
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
