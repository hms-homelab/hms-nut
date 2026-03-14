#include <gtest/gtest.h>
#include <curl/curl.h>
#include <json/json.h>
#include <string>
#include <sstream>

// E2E tests for DailySummaryService (needs running HMS-NUT at localhost:8891)

namespace {
size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}
}  // namespace

class DailySummaryE2ETest : public ::testing::Test {
protected:
    std::pair<int, std::string> httpPost(const std::string& url) {
        CURL* curl = curl_easy_init();
        std::string response;
        int http_code = 0;

        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

            CURLcode res = curl_easy_perform(curl);
            if (res == CURLE_OK) {
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            }
            curl_easy_cleanup(curl);
        }
        return {http_code, response};
    }

    std::pair<int, std::string> httpGet(const std::string& url) {
        CURL* curl = curl_easy_init();
        std::string response;
        int http_code = 0;

        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

            CURLcode res = curl_easy_perform(curl);
            if (res == CURLE_OK) {
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            }
            curl_easy_cleanup(curl);
        }
        return {http_code, response};
    }

    Json::Value parseJson(const std::string& str) {
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::istringstream stream(str);
        Json::parseFromStream(builder, stream, &root, &errors);
        return root;
    }
};

TEST_F(DailySummaryE2ETest, HealthEndpointShowsDailySummary) {
    auto [code, body] = httpGet("http://localhost:8891/health");
    if (code == 0) GTEST_SKIP() << "HMS-NUT service not running";

    EXPECT_EQ(code, 200);
    auto json = parseJson(body);
    EXPECT_TRUE(json["components"].isMember("daily_summary"));
    EXPECT_EQ(json["components"]["daily_summary"].asString(), "running");
}

TEST_F(DailySummaryE2ETest, ManualSummaryTrigger) {
    auto [code, body] = httpPost("http://localhost:8891/summary?date=2026-03-13");
    if (code == 0) GTEST_SKIP() << "HMS-NUT service not running";

    auto json = parseJson(body);
    EXPECT_TRUE(json["success"].asBool());
    EXPECT_EQ(json["date"].asString(), "2026-03-13");
    EXPECT_FALSE(json["summary"].asString().empty());

    std::string summary = json["summary"].asString();
    EXPECT_GT(summary.size(), 50u);
    EXPECT_LT(summary.size(), 5000u);
}

TEST_F(DailySummaryE2ETest, ManualSummaryDefaultsToYesterday) {
    auto [code, body] = httpPost("http://localhost:8891/summary");
    if (code == 0) GTEST_SKIP() << "HMS-NUT service not running";

    auto json = parseJson(body);
    EXPECT_FALSE(json["date"].asString().empty());
}

int main(int argc, char** argv) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    curl_global_cleanup();
    return result;
}
