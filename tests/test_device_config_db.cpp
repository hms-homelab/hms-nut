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
#include <chrono>
#include <cstdlib>
#include <pqxx/pqxx>
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

    // deleteDeviceConfig() deliberately retains the ups_devices row (history),
    // and ups_devices.device_name is UNIQUE, so a retained "Test UPS" row made
    // every later run fail. Remove what this test created: metrics (FK) first.
    void TearDown() override {
        if (!connected) return;
        DatabaseService::getInstance().deleteDeviceConfig(test_id);
        pqxx::connection c(
            "host=" + envOr("DB_HOST", "localhost") + " port=" + envOr("DB_PORT", "5432") +
            " dbname=" + envOr("DB_NAME", "ups_monitoring") + " user=" + envOr("DB_USER", "maestro") +
            " password=" + std::string(std::getenv("DB_PASSWORD")));
        pqxx::work txn(c);
        const std::string ids = "(SELECT device_id FROM ups_devices WHERE device_identifier = " +
                                txn.quote(test_id + "_db") + ")";
        txn.exec("DELETE FROM ups_metrics WHERE device_id IN " + ids);
        txn.exec("DELETE FROM ups_devices WHERE device_identifier = " + txn.quote(test_id + "_db"));
        txn.commit();
    }

    // Unique per run: ups_devices.device_name is UNIQUE.
    std::string friendlyName() const { return "Test UPS " + test_id; }
};

TEST_F(DeviceConfigDbTest, UpsertListEnableUpdateDelete) {
    auto& db = DatabaseService::getInstance();

    DeviceConfigRow row;
    row.mqtt_device_id = test_id;
    row.db_identifier  = test_id + "_db";
    row.friendly_name  = friendlyName();
    row.enabled        = true;
    ASSERT_TRUE(db.upsertDeviceConfig(row));

    // Present in the full list with the right fields
    bool found = false;
    for (const auto& r : db.listDeviceConfigs(true)) {
        if (r.mqtt_device_id == test_id) {
            found = true;
            EXPECT_EQ(r.db_identifier, test_id + "_db");
            EXPECT_EQ(r.friendly_name, friendlyName());
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
    row.friendly_name = "Renamed " + friendlyName();
    row.enabled = true;
    ASSERT_TRUE(db.upsertDeviceConfig(row));
    bool renamed = false;
    for (const auto& r : db.listDeviceConfigs(true)) {
        if (r.mqtt_device_id == test_id) renamed = (r.friendly_name == "Renamed " + friendlyName() && r.enabled);
    }
    EXPECT_TRUE(renamed);

    // Delete -> gone
    ASSERT_TRUE(db.deleteDeviceConfig(test_id));
    for (const auto& r : db.listDeviceConfigs(true)) {
        EXPECT_NE(r.mqtt_device_id, test_id);
    }
}

// battery_nominal_voltage was collected and shown live but never written by
// insertUpsMetrics() nor read by queryHistory(), so ups_metrics had no value
// for it after the old Python collector was retired (2026-02-14).
TEST_F(DeviceConfigDbTest, BatteryNominalVoltageRoundTrips) {
    auto& db = DatabaseService::getInstance();
    const std::string db_id = test_id + "_db";

    DeviceConfigRow row;
    row.mqtt_device_id = test_id;
    row.db_identifier  = db_id;
    row.friendly_name  = friendlyName();
    row.enabled        = true;
    ASSERT_TRUE(db.upsertDeviceConfig(row));  // ensures the ups_devices FK row

    UpsData data;
    data.timestamp = std::chrono::system_clock::now();
    data.battery_charge = 100.0;
    data.battery_voltage = 13.71;
    data.battery_nominal_voltage = 12.0;
    data.input_nominal_voltage = 120;
    ASSERT_TRUE(db.insertUpsMetrics(data, db_id));

    Json::Value hist = db.queryHistory(db_id, 1);
    ASSERT_TRUE(hist.isArray());
    ASSERT_EQ(hist.size(), 1u);
    ASSERT_TRUE(hist[0].isMember("battery_nominal_voltage"));
    EXPECT_DOUBLE_EQ(hist[0]["battery_nominal_voltage"].asDouble(), 12.0);
    EXPECT_DOUBLE_EQ(hist[0]["input_nominal_voltage"].asDouble(), 120.0);
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
