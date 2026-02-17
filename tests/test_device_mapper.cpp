#include <gtest/gtest.h>
#include "utils/DeviceMapper.h"
#include <cstdlib>

using namespace hms_nut;

class DeviceMapperTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset DeviceMapper state before each test
        DeviceMapper::reset();

        // Clear any existing environment variables
        unsetenv("UPS_DEVICE_IDS");
        unsetenv("UPS_DB_MAPPING");
        unsetenv("UPS_FRIENDLY_NAMES");
        unsetenv("NUT_DEVICE_ID");
    }

    void TearDown() override {
        // Clean up environment
        unsetenv("UPS_DEVICE_IDS");
        unsetenv("UPS_DB_MAPPING");
        unsetenv("UPS_FRIENDLY_NAMES");
        unsetenv("NUT_DEVICE_ID");

        // Reset for next test
        DeviceMapper::reset();
    }
};

TEST_F(DeviceMapperTest, InitializeWithSingleDevice) {
    setenv("UPS_DEVICE_IDS", "test_ups", 1);
    DeviceMapper::initialize();

    auto devices = DeviceMapper::getDeviceIds();
    ASSERT_EQ(devices.size(), 1);
    EXPECT_EQ(devices[0], "test_ups");
}

TEST_F(DeviceMapperTest, InitializeWithMultipleDevices) {
    setenv("UPS_DEVICE_IDS", "ups1,ups2,ups3", 1);
    DeviceMapper::initialize();

    auto devices = DeviceMapper::getDeviceIds();
    ASSERT_EQ(devices.size(), 3);
    EXPECT_EQ(devices[0], "ups1");
    EXPECT_EQ(devices[1], "ups2");
    EXPECT_EQ(devices[2], "ups3");
}

TEST_F(DeviceMapperTest, InitializeWithWhitespace) {
    setenv("UPS_DEVICE_IDS", "ups1 , ups2 , ups3", 1);
    DeviceMapper::initialize();

    auto devices = DeviceMapper::getDeviceIds();
    ASSERT_EQ(devices.size(), 3);
    EXPECT_EQ(devices[0], "ups1");
    EXPECT_EQ(devices[1], "ups2");
    EXPECT_EQ(devices[2], "ups3");
}

TEST_F(DeviceMapperTest, DbMappingFromJson) {
    setenv("UPS_DEVICE_IDS", "apc_bx", 1);
    setenv("UPS_DB_MAPPING", "{\"apc_bx\": \"apc_back_ups_xs_1000m\"}", 1);
    DeviceMapper::initialize();

    EXPECT_EQ(DeviceMapper::getDbIdentifier("apc_bx"), "apc_back_ups_xs_1000m");
}

TEST_F(DeviceMapperTest, DbMappingDefaultsToDeviceId) {
    setenv("UPS_DEVICE_IDS", "unknown_device", 1);
    DeviceMapper::initialize();

    // When no mapping is provided, should return the device ID as-is
    EXPECT_EQ(DeviceMapper::getDbIdentifier("unknown_device"), "unknown_device");
}

TEST_F(DeviceMapperTest, FriendlyNameFromJson) {
    setenv("UPS_DEVICE_IDS", "office_ups", 1);
    setenv("UPS_FRIENDLY_NAMES", "{\"office_ups\": \"Office UPS\"}", 1);
    DeviceMapper::initialize();

    EXPECT_EQ(DeviceMapper::getFriendlyName("office_ups"), "Office UPS");
}

TEST_F(DeviceMapperTest, FriendlyNameGeneration) {
    setenv("UPS_DEVICE_IDS", "my_custom_ups", 1);
    DeviceMapper::initialize();

    // Should generate a friendly name from device ID
    std::string friendly = DeviceMapper::getFriendlyName("my_custom_ups");
    EXPECT_EQ(friendly, "My custom ups");
}

TEST_F(DeviceMapperTest, IsKnownDevice) {
    setenv("UPS_DEVICE_IDS", "known_ups", 1);
    DeviceMapper::initialize();

    EXPECT_TRUE(DeviceMapper::isKnownDevice("known_ups"));
    EXPECT_FALSE(DeviceMapper::isKnownDevice("unknown_ups"));
}

TEST_F(DeviceMapperTest, FallbackToNutDeviceId) {
    setenv("NUT_DEVICE_ID", "fallback_ups", 1);
    DeviceMapper::initialize();

    auto devices = DeviceMapper::getDeviceIds();
    ASSERT_EQ(devices.size(), 1);
    EXPECT_EQ(devices[0], "fallback_ups");
}

TEST_F(DeviceMapperTest, AddDeviceAtRuntime) {
    setenv("UPS_DEVICE_IDS", "initial_ups", 1);
    DeviceMapper::initialize();

    DeviceConfig config;
    config.mqtt_device_id = "new_ups";
    config.db_identifier = "new_ups_db";
    config.friendly_name = "New UPS Device";
    DeviceMapper::addDevice(config);

    EXPECT_TRUE(DeviceMapper::isKnownDevice("new_ups"));
    EXPECT_EQ(DeviceMapper::getDbIdentifier("new_ups"), "new_ups_db");
    EXPECT_EQ(DeviceMapper::getFriendlyName("new_ups"), "New UPS Device");
}

TEST_F(DeviceMapperTest, ReverseMappingMqttToDb) {
    setenv("UPS_DEVICE_IDS", "mqtt_id", 1);
    setenv("UPS_DB_MAPPING", "{\"mqtt_id\": \"database_id\"}", 1);
    DeviceMapper::initialize();

    EXPECT_EQ(DeviceMapper::getMqttDeviceId("database_id"), "mqtt_id");
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
