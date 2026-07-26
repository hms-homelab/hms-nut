#pragma once

#include <string>
#include <map>
#include <optional>
#include <mutex>
#include <upsclient.h>

namespace hms_nut {

class NutClient {
public:
    NutClient(const std::string& host, int port, const std::string& ups_name);
    ~NutClient();

    // Disable copy
    NutClient(const NutClient&) = delete;
    NutClient& operator=(const NutClient&) = delete;

    bool connect();
    void disconnect();
    bool isConnected() const;

    // Get all UPS variables as key-value map
    std::map<std::string, std::string> getAllVariables();

    // Get single variable
    std::optional<std::string> getVariable(const std::string& var_name);

    // Get connection info
    std::string getHost() const { return host_; }
    int getPort() const { return port_; }
    std::string getUpsName() const { return ups_name_; }

private:
    bool reconnect();
    void logError(const std::string& operation);

    std::string host_;
    int port_;
    std::string ups_name_;
    UPSCONN_t* ups_conn_;
    mutable std::mutex mutex_;
    bool connected_;
    int reconnect_attempts_;
    static constexpr int MAX_RECONNECT_BACKOFF_SEC = 64;
};

}  // namespace hms_nut
