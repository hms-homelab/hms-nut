#include "services/DailySummaryService.h"
#include "utils/DeviceMapper.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace hms_nut {

DailySummaryService::DailySummaryService(std::shared_ptr<MqttClient> mqtt_client,
                                         DatabaseService& db_service,
                                         const hms::LLMConfig& llm_config,
                                         int summary_hour,
                                         const std::string& prompt_file)
    : mqtt_client_(mqtt_client),
      db_service_(db_service),
      summary_hour_(summary_hour),
      prompt_file_(prompt_file) {

    if (llm_config.enabled) {
        llm_client_ = std::make_unique<hms::LLMClient>(llm_config);
        std::cout << "🤖 DailySummary: LLM enabled ("
                  << hms::LLMClient::providerName(llm_config.provider)
                  << ", model: " << llm_config.model << ")" << std::endl;
    } else {
        std::cout << "🤖 DailySummary: LLM disabled" << std::endl;
    }

    // Load prompt template
    prompt_template_ = hms::LLMClient::loadPromptFile(prompt_file_);
    if (prompt_template_.empty()) {
        std::cout << "⚠️  DailySummary: No prompt file at " << prompt_file_
                  << ", using default template" << std::endl;
        prompt_template_ =
            "You are a power systems analyst reviewing UPS monitoring data for a home lab.\n"
            "Provide a concise daily summary of the UPS energy status. Include:\n\n"
            "1. Overall power quality assessment (excellent/good/fair/poor)\n"
            "2. Voltage stability (dips, spikes, or steady)\n"
            "3. Load analysis (per device if multiple)\n"
            "4. Battery health status\n"
            "5. Any power events or anomalies\n"
            "6. Notable trends or concerns\n\n"
            "Keep the summary to 4-6 sentences. Be direct and actionable.\n"
            "Do not use markdown formatting. Do not include disclaimers.\n\n"
            "UPS data:\n{metrics}";
    } else {
        std::cout << "🤖 DailySummary: Loaded prompt template from " << prompt_file_ << std::endl;
    }

    std::cout << "🤖 DailySummary: Configured for " << summary_hour_ << ":00 daily" << std::endl;
}

DailySummaryService::~DailySummaryService() {
    stop();
}

void DailySummaryService::start() {
    if (running_) return;

    if (!llm_client_) {
        std::cout << "🤖 DailySummary: Not starting (LLM disabled)" << std::endl;
        return;
    }

    running_ = true;
    timer_thread_ = std::thread(&DailySummaryService::timerLoop, this);
    std::cout << "🤖 DailySummary: Started (daily at " << summary_hour_ << ":00)" << std::endl;
}

void DailySummaryService::stop() {
    if (!running_) return;

    std::cout << "🤖 DailySummary: Stopping..." << std::endl;
    running_ = false;

    if (timer_thread_.joinable()) {
        timer_thread_.join();
    }

    std::cout << "🤖 DailySummary: Stopped" << std::endl;
}

std::chrono::system_clock::time_point DailySummaryService::getLastSummaryTime() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return last_summary_time_;
}

std::string DailySummaryService::getLastSummary() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return last_summary_;
}

void DailySummaryService::publishDiscovery() {
    if (!mqtt_client_ || !mqtt_client_->isConnected()) return;

    // Publish HA MQTT discovery for the daily summary sensor
    std::string config_topic = "homeassistant/sensor/hms_nut/daily_energy_summary/config";
    std::string config_payload =
        "{"
        "\"name\":\"Daily Energy Summary\","
        "\"state_topic\":\"homeassistant/sensor/hms_nut/daily_energy_summary/state\","
        "\"unique_id\":\"hms_nut_daily_energy_summary\","
        "\"device\":{"
            "\"identifiers\":[\"hms_nut\"],"
            "\"name\":\"HMS-NUT\","
            "\"manufacturer\":\"HMS Homelab\","
            "\"model\":\"UPS Monitor\""
        "},"
        "\"icon\":\"mdi:lightning-bolt-circle\""
        "}";

    mqtt_client_->publish(config_topic, config_payload, 1, true);
    std::cout << "🤖 DailySummary: Published HA discovery config" << std::endl;
}

void DailySummaryService::timerLoop() {
    std::cout << "🤖 DailySummary: Timer thread started" << std::endl;

    while (running_) {
        // Get current local time
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::tm local_tm;
        localtime_r(&time_t_now, &local_tm);

        int current_hour = local_tm.tm_hour;

        // Build today's date string for dedup check
        std::ostringstream today_oss;
        today_oss << std::put_time(&local_tm, "%Y-%m-%d");
        std::string today_str = today_oss.str();

        // Check if it's time and we haven't already run today
        if (current_hour == summary_hour_ && last_summary_date_ != today_str) {
            // Calculate yesterday's date
            auto yesterday = now - std::chrono::hours(24);
            auto yesterday_t = std::chrono::system_clock::to_time_t(yesterday);
            std::tm yesterday_tm;
            localtime_r(&yesterday_t, &yesterday_tm);

            std::ostringstream yesterday_oss;
            yesterday_oss << std::put_time(&yesterday_tm, "%Y-%m-%d");
            std::string yesterday_str = yesterday_oss.str();

            std::cout << "🤖 DailySummary: Generating summary for " << yesterday_str << std::endl;

            if (generateSummary(yesterday_str)) {
                last_summary_date_ = today_str;
            }
        }

        // Sleep 30 seconds between checks (precise enough, low overhead)
        for (int i = 0; i < 30 && running_; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    std::cout << "🤖 DailySummary: Timer thread stopped" << std::endl;
}

bool DailySummaryService::generateSummary(const std::string& date) {
    if (!llm_client_) {
        std::cerr << "⚠️  DailySummary: LLM not configured" << std::endl;
        return false;
    }

    // Query yesterday's metrics from PostgreSQL
    std::string metrics = db_service_.queryDailyMetrics(date);
    if (metrics.empty()) {
        std::cerr << "⚠️  DailySummary: No metrics data for " << date << std::endl;
        return false;
    }

    std::cout << "🤖 DailySummary: Queried metrics (" << metrics.size() << " bytes)" << std::endl;

    // Build prompt from template
    std::string prompt = buildPrompt(metrics);

    // Call LLM
    std::cout << "🤖 DailySummary: Calling LLM..." << std::endl;
    auto result = llm_client_->generate(prompt);

    if (!result) {
        std::cerr << "❌ DailySummary: LLM call failed" << std::endl;
        return false;
    }

    std::string summary = *result;
    std::cout << "🤖 DailySummary: Got summary (" << summary.size() << " chars)" << std::endl;

    // Publish to MQTT
    std::string state_topic = "homeassistant/sensor/hms_nut/daily_energy_summary/state";
    if (mqtt_client_ && mqtt_client_->isConnected()) {
        mqtt_client_->publish(state_topic, summary, 1, true);
        std::cout << "🤖 DailySummary: Published to " << state_topic << std::endl;
    } else {
        std::cerr << "⚠️  DailySummary: MQTT not connected, summary not published" << std::endl;
    }

    // Update state
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_summary_time_ = std::chrono::system_clock::now();
        last_summary_ = summary;
    }

    return true;
}

std::string DailySummaryService::buildPrompt(const std::string& metrics) {
    return hms::LLMClient::substituteTemplate(
        prompt_template_,
        {{"metrics", metrics}});
}

}  // namespace hms_nut
