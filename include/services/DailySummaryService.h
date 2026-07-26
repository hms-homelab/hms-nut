#pragma once

#include "mqtt/MqttClient.h"
#include "database/DatabaseService.h"
#include "llm_client.h"
#include <memory>
#include <thread>
#include <atomic>
#include <string>
#include <chrono>

namespace hms_nut {

/**
 * DailySummaryService - Daily UPS energy summary via LLM
 *
 * At a configurable hour each morning (default 7 AM), queries PostgreSQL
 * for yesterday's UPS metrics across all devices, sends the data to an
 * LLM for a natural language summary, and publishes the result to MQTT.
 */
class DailySummaryService {
public:
    /**
     * Constructor
     *
     * @param mqtt_client Shared MQTT client for publishing summaries
     * @param db_service Database service for querying metrics
     * @param llm_config LLM configuration (provider, model, endpoint)
     * @param summary_hour Hour of day to generate summary (0-23, default 7)
     * @param prompt_file Path to prompt template file (must contain {metrics})
     */
    DailySummaryService(std::shared_ptr<MqttClient> mqtt_client,
                        DatabaseService& db_service,
                        const hms::LLMConfig& llm_config,
                        int summary_hour = 7,
                        const std::string& prompt_file = "llm_prompt.txt");

    ~DailySummaryService();

    DailySummaryService(const DailySummaryService&) = delete;
    DailySummaryService& operator=(const DailySummaryService&) = delete;

    /// Start the background timer thread
    void start();

    /// Stop the service gracefully
    void stop();

    /// Check if service is running
    bool isRunning() const { return running_; }

    /// Get last summary generation time
    std::chrono::system_clock::time_point getLastSummaryTime() const;

    /// Get last summary text
    std::string getLastSummary() const;

    /// Publish HA MQTT discovery config for the summary sensor
    void publishDiscovery();

    /// Manually trigger summary generation for a specific date (YYYY-MM-DD)
    bool generateSummary(const std::string& date);

private:
    /// Background loop that checks the clock and triggers daily summary
    void timerLoop();

    /// Build the full prompt from template + metrics
    std::string buildPrompt(const std::string& metrics);

    // Dependencies
    std::shared_ptr<MqttClient> mqtt_client_;
    DatabaseService& db_service_;
    std::unique_ptr<hms::LLMClient> llm_client_;

    // Configuration
    int summary_hour_;
    std::string prompt_file_;
    std::string prompt_template_;

    // State
    std::thread timer_thread_;
    std::atomic<bool> running_{false};
    std::string last_summary_date_;  // prevents duplicate runs
    mutable std::mutex state_mutex_;
    std::chrono::system_clock::time_point last_summary_time_;
    std::string last_summary_;
};

}  // namespace hms_nut
