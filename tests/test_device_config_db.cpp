// Integration test for the device_config persistence layer.
//
// Requires a reachable PostgreSQL. Connection is taken from the same env vars
// the service uses (DB_HOST/DB_PORT/DB_NAME/DB_USER/DB_PASSWORD). No credentials
// are hardcoded (public repo). The test SKIPs when DB_PASSWORD is unset or the
// database is unreachable, and cleans up its own row on teardown.
//
//   DB_PASSWORD=... ./test_device_config_db

#include <gtest/gtest.h>
#include "database/DatabaseService.h"
#include "nut/UpsData.h"
#include <cstdlib>
#include <string>
#include <unistd.h>

using namespace hms_nut;

static std::string envOr(const char* key, const char* def) {
    const char* v = std::getenv(key);
    return (v && *v) ? std::string(v) : std::string(def);
}

class DeviceConfigDbTest : public ::testing::Test {
protected:
    std::string test_id;
    bool connected = false;

    void SetUp() override {
        const char* pw = std::getenv("DB_PASSWORD");
        if (!pw || !*pw) {
            GTEST_SKIP() << "DB_PASSWORD not set — skipping DB integration test";
        }
        std::string conn =
            "host=" + envOr("DB_HOST", "localhost") +
            " port=" + envOr("DB_PORT", "5432") +
            " dbname=" + envOr("DB_NAME", "ups_monitoring") +
            " user=" + envOr("DB_USER", "maestro") +
            " password=" + pw;

        auto& db = DatabaseService::getInstance();
        db.initialize(conn);
        if (!db.isConnected()) {
            GTEST_SKIP() << "PostgreSQL not reachable — skipping DB integration test";
        }
        connected = true;
        db.ensureDeviceConfigTable();
        test_id = "test_ups_" + std::to_string(getpid());
        db.deleteDeviceConfig(test_id);  // clean slate
    }

    void TearDown() override {
        if (connected) {
            DatabaseService::getInstance().deleteDeviceConfig(test_id);
        }
    }
};

TEST_F(DeviceConfigDbTest, UpsertListEnableUpdateDelete) {
    auto& db = DatabaseService::getInstance();

    DeviceConfigRow row;
    row.mqtt_device_id = test_id;
    row.db_identifier  = test_id + "_db";
    row.friendly_name  = "Test UPS";
    row.enabled        = true;
    ASSERT_TRUE(db.upsertDeviceConfig(row));

    // Present in the full list with the right fields
    bool found = false;
    for (const auto& r : db.listDeviceConfigs(true)) {
        if (r.mqtt_device_id == test_id) {
            found = true;
            EXPECT_EQ(r.db_identifier, test_id + "_db");
            EXPECT_EQ(r.friendly_name, "Test UPS");
            EXPECT_TRUE(r.enabled);
        }
    }
    EXPECT_TRUE(found);

    // upsert also ensured an ups_devices row (FK target for metrics)
    EXPECT_TRUE(db.getDeviceId(test_id + "_db").has_value());

    // Disable -> excluded from the enabled-only list
    ASSERT_TRUE(db.setDeviceEnabled(test_id, false));
    for (const auto& r : db.listDeviceConfigs(false)) {
        EXPECT_NE(r.mqtt_device_id, test_id);
    }

    // Re-upsert with a new name + re-enable
    row.friendly_name = "Renamed UPS";
    row.enabled = true;
    ASSERT_TRUE(db.upsertDeviceConfig(row));
    bool renamed = false;
    for (const auto& r : db.listDeviceConfigs(true)) {
        if (r.mqtt_device_id == test_id) renamed = (r.friendly_name == "Renamed UPS" && r.enabled);
    }
    EXPECT_TRUE(renamed);

    // Delete -> gone
    ASSERT_TRUE(db.deleteDeviceConfig(test_id));
    for (const auto& r : db.listDeviceConfigs(true)) {
        EXPECT_NE(r.mqtt_device_id, test_id);
    }
}

TEST_F(DeviceConfigDbTest, HistoryAndEventsReturnArrays) {
    auto& db = DatabaseService::getInstance();
    Json::Value hist = db.queryHistory(test_id + "_db", 24);
    EXPECT_TRUE(hist.isArray());  // empty is fine; must be a well-formed array
    Json::Value events = db.queryRecentEvents("", 10);
    EXPECT_TRUE(events.isArray());
}

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
