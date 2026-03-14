#include <gtest/gtest.h>
#include "llm_client.h"
#include "database/DatabaseService.h"
#include <string>

// ═══════════════════════════════════════════════════════════════════════
// Unit tests for DailySummaryService components (no live services needed)
// ═══════════════════════════════════════════════════════════════════════

class LLMClientUnitTest : public ::testing::Test {};

// --- Provider parsing ---

TEST_F(LLMClientUnitTest, ParseProviderOllama) {
    EXPECT_EQ(hms::LLMClient::parseProvider("ollama"), hms::LLMProvider::OLLAMA);
}

TEST_F(LLMClientUnitTest, ParseProviderOpenAI) {
    EXPECT_EQ(hms::LLMClient::parseProvider("openai"), hms::LLMProvider::OPENAI);
    EXPECT_EQ(hms::LLMClient::parseProvider("chatgpt"), hms::LLMProvider::OPENAI);
}

TEST_F(LLMClientUnitTest, ParseProviderGemini) {
    EXPECT_EQ(hms::LLMClient::parseProvider("gemini"), hms::LLMProvider::GEMINI);
    EXPECT_EQ(hms::LLMClient::parseProvider("google"), hms::LLMProvider::GEMINI);
}

TEST_F(LLMClientUnitTest, ParseProviderAnthropic) {
    EXPECT_EQ(hms::LLMClient::parseProvider("anthropic"), hms::LLMProvider::ANTHROPIC);
    EXPECT_EQ(hms::LLMClient::parseProvider("claude"), hms::LLMProvider::ANTHROPIC);
}

TEST_F(LLMClientUnitTest, ParseProviderDefaultsToOllama) {
    EXPECT_EQ(hms::LLMClient::parseProvider("unknown"), hms::LLMProvider::OLLAMA);
    EXPECT_EQ(hms::LLMClient::parseProvider(""), hms::LLMProvider::OLLAMA);
}

// --- Provider name ---

TEST_F(LLMClientUnitTest, ProviderNameOllama) {
    EXPECT_EQ(hms::LLMClient::providerName(hms::LLMProvider::OLLAMA), "ollama");
}

TEST_F(LLMClientUnitTest, ProviderNameOpenAI) {
    EXPECT_EQ(hms::LLMClient::providerName(hms::LLMProvider::OPENAI), "openai");
}

// --- Template substitution ---

TEST_F(LLMClientUnitTest, SubstituteTemplateBasic) {
    std::string tmpl = "Hello {name}, you have {count} messages.";
    auto result = hms::LLMClient::substituteTemplate(tmpl, {
        {"name", "Albin"},
        {"count", "5"}
    });
    EXPECT_EQ(result, "Hello Albin, you have 5 messages.");
}

TEST_F(LLMClientUnitTest, SubstituteTemplateMetrics) {
    std::string tmpl = "Analyze this data:\n{metrics}";
    std::string metrics = "Device: APC UPS\n  Load: 20%\n  Battery: 100%";
    auto result = hms::LLMClient::substituteTemplate(tmpl, {{"metrics", metrics}});
    EXPECT_EQ(result, "Analyze this data:\nDevice: APC UPS\n  Load: 20%\n  Battery: 100%");
}

TEST_F(LLMClientUnitTest, SubstituteTemplateNoPlaceholders) {
    std::string tmpl = "No placeholders here.";
    auto result = hms::LLMClient::substituteTemplate(tmpl, {{"metrics", "data"}});
    EXPECT_EQ(result, "No placeholders here.");
}

TEST_F(LLMClientUnitTest, SubstituteTemplateEmptyMetrics) {
    std::string tmpl = "Data:\n{metrics}";
    auto result = hms::LLMClient::substituteTemplate(tmpl, {{"metrics", ""}});
    EXPECT_EQ(result, "Data:\n");
}

// --- Prompt file loading ---

TEST_F(LLMClientUnitTest, LoadPromptFileNotFound) {
    auto result = hms::LLMClient::loadPromptFile("/nonexistent/path/prompt.txt");
    EXPECT_TRUE(result.empty());
}

TEST_F(LLMClientUnitTest, LoadPromptFileExists) {
    // The llm_prompt.txt is at the project root (two dirs up from tests/build/)
    std::string prompt_path = std::string(__FILE__);
    prompt_path = prompt_path.substr(0, prompt_path.rfind('/'));  // tests/
    prompt_path = prompt_path.substr(0, prompt_path.rfind('/'));  // project root
    prompt_path += "/llm_prompt.txt";

    auto result = hms::LLMClient::loadPromptFile(prompt_path);
    EXPECT_FALSE(result.empty());
    EXPECT_NE(result.find("{metrics}"), std::string::npos);
}

// --- LLM config defaults ---

TEST_F(LLMClientUnitTest, DefaultConfigDisabled) {
    hms::LLMConfig config;
    EXPECT_FALSE(config.enabled);
    EXPECT_EQ(config.provider, hms::LLMProvider::OLLAMA);
    EXPECT_EQ(config.keep_alive_seconds, 0);
    EXPECT_EQ(config.temperature, 0.3);
    EXPECT_EQ(config.max_tokens, 1024);
}

TEST_F(LLMClientUnitTest, ClientReportsDisabledWhenNotEnabled) {
    hms::LLMConfig config;
    config.enabled = false;
    hms::LLMClient client(config);
    EXPECT_FALSE(client.isEnabled());
}

TEST_F(LLMClientUnitTest, ClientReportsEnabledWhenEnabled) {
    hms::LLMConfig config;
    config.enabled = true;
    hms::LLMClient client(config);
    EXPECT_TRUE(client.isEnabled());
}

// ═══════════════════════════════════════════════════════════════════════
// Integration tests (need PostgreSQL at localhost:5432/ups_monitoring)
// ═══════════════════════════════════════════════════════════════════════

class DatabaseDailySummaryTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto& db = hms_nut::DatabaseService::getInstance();
        db.initialize("host=localhost port=5432 dbname=ups_monitoring "
                      "user=maestro password=maestro_postgres_2026_secure");
        connected_ = db.isConnected();
    }

    bool connected_ = false;
};

TEST_F(DatabaseDailySummaryTest, QueryDailyMetricsReturnsData) {
    if (!connected_) GTEST_SKIP() << "PostgreSQL not available";

    auto& db = hms_nut::DatabaseService::getInstance();

    // Query a date we know has data (today or recent)
    std::string result = db.queryDailyMetrics("2026-03-13");

    // Should either have data or say "No UPS metrics data found"
    EXPECT_FALSE(result.empty());

    // If data exists, should contain device info
    if (result.find("No UPS metrics") == std::string::npos) {
        EXPECT_NE(result.find("Device:"), std::string::npos);
        EXPECT_NE(result.find("Readings:"), std::string::npos);
        EXPECT_NE(result.find("Input Voltage:"), std::string::npos);
        EXPECT_NE(result.find("Load:"), std::string::npos);
        EXPECT_NE(result.find("Battery:"), std::string::npos);
        EXPECT_NE(result.find("Power Failures:"), std::string::npos);
    }
}

TEST_F(DatabaseDailySummaryTest, QueryDailyMetricsNoDataReturnsMessage) {
    if (!connected_) GTEST_SKIP() << "PostgreSQL not available";

    auto& db = hms_nut::DatabaseService::getInstance();

    // Query a date with no data
    std::string result = db.queryDailyMetrics("2020-01-01");
    EXPECT_NE(result.find("No UPS metrics data found"), std::string::npos);
}

TEST_F(DatabaseDailySummaryTest, QueryDailyMetricsContainsAllDevices) {
    if (!connected_) GTEST_SKIP() << "PostgreSQL not available";

    auto& db = hms_nut::DatabaseService::getInstance();
    std::string result = db.queryDailyMetrics("2026-03-13");

    if (result.find("No UPS metrics") == std::string::npos) {
        // Should contain the HMS-NUT UPS at minimum
        EXPECT_NE(result.find("apc_bx"), std::string::npos);
    }
}

TEST_F(DatabaseDailySummaryTest, QueryDailyMetricsFormatsVoltageRange) {
    if (!connected_) GTEST_SKIP() << "PostgreSQL not available";

    auto& db = hms_nut::DatabaseService::getInstance();
    std::string result = db.queryDailyMetrics("2026-03-13");

    if (result.find("No UPS metrics") == std::string::npos) {
        // Should have voltage range format: "XXX.XV - XXX.XV (avg XXX.XV)"
        EXPECT_NE(result.find("V -"), std::string::npos);
        EXPECT_NE(result.find("(avg"), std::string::npos);
    }
}

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
