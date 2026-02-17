#include "nut/NutClient.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <vector>

namespace hms_nut {

NutClient::NutClient(const std::string& host, int port, const std::string& ups_name)
    : host_(host),
      port_(port),
      ups_name_(ups_name),
      ups_conn_(nullptr),
      connected_(false),
      reconnect_attempts_(0) {
}

NutClient::~NutClient() {
    disconnect();
}

bool NutClient::connect() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (connected_ && ups_conn_) {
        return true;
    }

    // Initialize UPS connection
    ups_conn_ = new UPSCONN_t;
    memset(ups_conn_, 0, sizeof(UPSCONN_t));

    int result = upscli_connect(ups_conn_, host_.c_str(), port_, UPSCLI_CONN_TRYSSL);
    if (result < 0) {
        logError("connect");
        delete ups_conn_;
        ups_conn_ = nullptr;
        connected_ = false;

        // Exponential backoff
        int backoff_sec = std::min(1 << reconnect_attempts_, MAX_RECONNECT_BACKOFF_SEC);
        reconnect_attempts_++;
        std::cerr << "🔄 NUT: Reconnecting in " << backoff_sec << "s..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(backoff_sec));

        return false;
    }

    connected_ = true;
    reconnect_attempts_ = 0;  // Reset on successful connection
    std::cout << "✅ NUT: Connected to " << host_ << ":" << port_
              << " (UPS: " << ups_name_ << ")" << std::endl;
    return true;
}

void NutClient::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (ups_conn_) {
        upscli_disconnect(ups_conn_);
        delete ups_conn_;
        ups_conn_ = nullptr;
    }

    connected_ = false;
}

bool NutClient::isConnected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connected_ && ups_conn_;
}

bool NutClient::reconnect() {
    disconnect();
    return connect();
}

std::map<std::string, std::string> NutClient::getAllVariables() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::map<std::string, std::string> variables;

    if (!connected_ || !ups_conn_) {
        std::cerr << "❌ NUT: Not connected" << std::endl;
        return variables;
    }

    // Parse UPS name to extract actual name (format: "upsname@hostname")
    std::string ups_name = ups_name_;
    size_t at_pos = ups_name.find('@');
    if (at_pos != std::string::npos) {
        ups_name = ups_name.substr(0, at_pos);
    }

    // Use upsc command (more reliable than libupsclient API)
    std::string command = "upsc " + ups_name + " 2>/dev/null";
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe) {
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe)) {
            std::string line(buffer);
            // Parse line: "variable.name: value"
            size_t colon_pos = line.find(':');
            if (colon_pos != std::string::npos) {
                std::string var_name = line.substr(0, colon_pos);
                std::string var_value = line.substr(colon_pos + 1);

                // Trim whitespace
                var_name.erase(0, var_name.find_first_not_of(" \t\n\r"));
                var_name.erase(var_name.find_last_not_of(" \t\n\r") + 1);
                var_value.erase(0, var_value.find_first_not_of(" \t\n\r"));
                var_value.erase(var_value.find_last_not_of(" \t\n\r") + 1);

                if (!var_name.empty() && !var_value.empty()) {
                    variables[var_name] = var_value;
                }
            }
        }
        pclose(pipe);
    }

    if (variables.empty()) {
        std::cerr << "⚠️  NUT: No variables retrieved from UPS" << std::endl;
    }

    return variables;
}

std::optional<std::string> NutClient::getVariable(const std::string& var_name) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!connected_ || !ups_conn_) {
        return std::nullopt;
    }

    // Parse UPS name
    std::string ups_name = ups_name_;
    size_t at_pos = ups_name.find('@');
    if (at_pos != std::string::npos) {
        ups_name = ups_name.substr(0, at_pos);
    }

    size_t numa = 2;
    char** answer = nullptr;
    const char* query[2] = {ups_name.c_str(), var_name.c_str()};
    int result = upscli_get(ups_conn_, numa, query, &numa, &answer);

    if (result > 0 && numa > 0 && answer && answer[0]) {
        return std::string(answer[0]);
    }

    return std::nullopt;
}

void NutClient::logError(const std::string& operation) {
    if (ups_conn_) {
        std::cerr << "❌ NUT " << operation << " error: "
                  << upscli_strerror(ups_conn_) << " ("
                  << upscli_upserror(ups_conn_) << ")" << std::endl;
    } else {
        std::cerr << "❌ NUT " << operation << " error: Connection not initialized" << std::endl;
    }
}

}  // namespace hms_nut
