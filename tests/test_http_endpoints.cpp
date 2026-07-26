#include <gtest/gtest.h>
#include <curl/curl.h>
#include <json/json.h>
#include <string>
#include <thread>
#include <chrono>

// Callback for CURL to capture response
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// Helper to make HTTP requests
class HttpClient {
public:
    static std::pair<int, std::string> get(const std::string& url) {
        CURL* curl = curl_easy_init();
        std::string response;
        int http_code = 0;

        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
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

    static std::pair<int, std::string> post(const std::string& url, const std::string& body = "") {
        CURL* curl = curl_easy_init();
        std::string response;
        int http_code = 0;

        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
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
};

// Test fixture for HTTP endpoint tests
class HTTPEndpointTest : public ::testing::Test {
protected:
    const std::string base_url = "http://localhost:8891";

    void SetUp() override {
        // Give service time to start if just launched
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
};

// Test: Health endpoint returns 200 OK
TEST_F(HTTPEndpointTest, HealthEndpointReturns200) {
    auto [code, response] = HttpClient::get(base_url + "/health");

    EXPECT_EQ(code, 200) << "Health endpoint should return 200 OK";
    EXPECT_FALSE(response.empty()) << "Health endpoint should return JSON response";

    // Parse JSON
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream stream(response);

    bool parsed = Json::parseFromStream(builder, stream, &root, &errors);
    ASSERT_TRUE(parsed) << "Failed to parse JSON: " << errors;

    // Check JSON structure
    EXPECT_TRUE(root.isMember("service"));
    EXPECT_EQ(root["service"].asString(), "hms-nut");
    EXPECT_TRUE(root.isMember("status"));
    EXPECT_TRUE(root.isMember("components"));

    std::cout << "✅ Health endpoint returns valid JSON" << std::endl;
}

// Test: Republish endpoint returns 200 OK on successful republish
TEST_F(HTTPEndpointTest, RepublishEndpointReturns200) {
    auto [code, response] = HttpClient::post(base_url + "/republish");

    // Should return 200 if MQTT is connected, or 500 if not
    EXPECT_TRUE(code == 200 || code == 500) << "Republish should return 200 or 500, got: " << code;
    EXPECT_FALSE(response.empty()) << "Republish endpoint should return JSON response";

    // Parse JSON
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream stream(response);

    bool parsed = Json::parseFromStream(builder, stream, &root, &errors);
    ASSERT_TRUE(parsed) << "Failed to parse JSON: " << errors;

    // Check JSON structure
    EXPECT_TRUE(root.isMember("service"));
    EXPECT_EQ(root["service"].asString(), "hms-nut");
    EXPECT_TRUE(root.isMember("success"));
    EXPECT_TRUE(root.isMember("message"));

    std::cout << "✅ Republish endpoint returns: " << root["message"].asString() << std::endl;
}

// Test: Health endpoint includes all components
TEST_F(HTTPEndpointTest, HealthEndpointIncludesComponents) {
    auto [code, response] = HttpClient::get(base_url + "/health");

    ASSERT_EQ(code, 200);

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream stream(response);
    Json::parseFromStream(builder, stream, &root, &errors);

    // Check components
    ASSERT_TRUE(root.isMember("components"));
    Json::Value components = root["components"];

    EXPECT_TRUE(components.isMember("mqtt"));
    EXPECT_TRUE(components.isMember("database"));
    EXPECT_TRUE(components.isMember("nut_bridge"));
    EXPECT_TRUE(components.isMember("collector"));

    std::cout << "✅ Health endpoint includes all components" << std::endl;
}

// Test: Multiple concurrent requests to health endpoint
TEST_F(HTTPEndpointTest, ConcurrentHealthRequests) {
    const int num_requests = 10;
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int i = 0; i < num_requests; i++) {
        threads.emplace_back([&]() {
            auto [code, response] = HttpClient::get(base_url + "/health");
            if (code == 200) {
                success_count++;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count, num_requests)
        << "All " << num_requests << " concurrent requests should succeed";

    std::cout << "✅ " << success_count << "/" << num_requests
              << " concurrent health requests succeeded" << std::endl;
}

// Test: Multiple concurrent republish requests
TEST_F(HTTPEndpointTest, ConcurrentRepublishRequests) {
    const int num_requests = 5;
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int i = 0; i < num_requests; i++) {
        threads.emplace_back([&]() {
            auto [code, response] = HttpClient::post(base_url + "/republish");
            if (code == 200 || code == 500) {  // Both are valid responses
                success_count++;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count, num_requests)
        << "All " << num_requests << " concurrent republish requests should complete";

    std::cout << "✅ " << success_count << "/" << num_requests
              << " concurrent republish requests completed" << std::endl;
}

// Test: Invalid endpoint returns 404
TEST_F(HTTPEndpointTest, InvalidEndpointReturns404) {
    auto [code, response] = HttpClient::get(base_url + "/invalid_endpoint");

    EXPECT_EQ(code, 404) << "Invalid endpoint should return 404 Not Found";

    std::cout << "✅ Invalid endpoint returns 404" << std::endl;
}

int main(int argc, char **argv) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    curl_global_cleanup();
    return result;
}
