#include <gtest/gtest.h>
#include "nut/UpsData.h"
#include <map>
#include <string>

using namespace hms_nut;

class UpsDataTest : public ::testing::Test {
protected:
    std::map<std::string, std::string> createValidNutVariables() {
        return {
            {"battery.charge", "100"},
            {"battery.voltage", "13.7"},
            {"battery.runtime", "2400"},
            {"input.voltage", "121.0"},
            {"ups.status", "OL"},
            {"ups.load", "25"}
        };
    }
};

TEST_F(UpsDataTest, CreateFromNutVariables) {
    auto vars = createValidNutVariables();
    UpsData data = UpsData::fromNutVariables("test_ups", vars);

    EXPECT_EQ(data.device_id, "test_ups");
    EXPECT_TRUE(data.battery_charge.has_value());
    EXPECT_DOUBLE_EQ(data.battery_charge.value(), 100.0);
    EXPECT_TRUE(data.battery_voltage.has_value());
    EXPECT_DOUBLE_EQ(data.battery_voltage.value(), 13.7);
    EXPECT_TRUE(data.battery_runtime.has_value());
    EXPECT_EQ(data.battery_runtime.value(), 2400);
}

TEST_F(UpsDataTest, ValidDataWithRequiredFields) {
    UpsData data;
    data.device_id = "test_ups";
    data.battery_charge = 85.0;
    data.input_voltage = 120.0;
    data.ups_status = "OL";

    EXPECT_TRUE(data.isValid());
}

TEST_F(UpsDataTest, InvalidDataMissingDeviceId) {
    UpsData data;
    data.battery_charge = 85.0;
    data.input_voltage = 120.0;

    EXPECT_FALSE(data.isValid());
}

TEST_F(UpsDataTest, InvalidDataOutOfRangeBatteryCharge) {
    UpsData data;
    data.device_id = "test_ups";
    data.battery_charge = 150.0;  // Invalid: > 100%
    data.input_voltage = 120.0;

    EXPECT_FALSE(data.isValid());
}

TEST_F(UpsDataTest, InvalidDataNegativeBatteryCharge) {
    UpsData data;
    data.device_id = "test_ups";
    data.battery_charge = -10.0;  // Invalid: negative
    data.input_voltage = 120.0;

    EXPECT_FALSE(data.isValid());
}

TEST_F(UpsDataTest, UpdateFieldFromMqtt) {
    UpsData data;
    data.device_id = "test_ups";

    data.updateFieldFromMqtt("battery_charge", "95.5");
    EXPECT_TRUE(data.battery_charge.has_value());
    EXPECT_DOUBLE_EQ(data.battery_charge.value(), 95.5);

    data.updateFieldFromMqtt("input_voltage", "118.0");
    EXPECT_TRUE(data.input_voltage.has_value());
    EXPECT_DOUBLE_EQ(data.input_voltage.value(), 118.0);

    data.updateFieldFromMqtt("ups_status", "OL");
    EXPECT_TRUE(data.ups_status.has_value());
    EXPECT_EQ(data.ups_status.value(), "OL");
}

TEST_F(UpsDataTest, ToMqttMessages) {
    UpsData data;
    data.device_id = "apc_ups";
    data.battery_charge = 100.0;
    data.input_voltage = 121.0;
    data.ups_status = "OL";

    auto messages = data.toMqttMessages();

    EXPECT_GT(messages.size(), 0);

    // Check that battery_charge message exists
    bool found_battery_charge = false;
    for (const auto& msg : messages) {
        if (msg.topic.find("battery_charge") != std::string::npos) {
            found_battery_charge = true;
            // Payload should contain "100" (formatting may vary)
            EXPECT_NE(msg.payload.find("100"), std::string::npos);
            EXPECT_EQ(msg.qos, 1);
            break;
        }
    }
    EXPECT_TRUE(found_battery_charge);
}

TEST_F(UpsDataTest, ToJson) {
    UpsData data;
    data.device_id = "test_ups";
    data.battery_charge = 80.0;
    data.battery_voltage = 13.5;
    data.input_voltage = 119.0;
    data.ups_status = "OL";

    std::string json = data.toJson();

    // Verify JSON contains expected fields
    EXPECT_NE(json.find("device_id"), std::string::npos);
    EXPECT_NE(json.find("test_ups"), std::string::npos);
    EXPECT_NE(json.find("battery_charge"), std::string::npos);
    EXPECT_NE(json.find("80"), std::string::npos);
}

TEST_F(UpsDataTest, HandleMissingOptionalFields) {
    std::map<std::string, std::string> vars = {
        {"battery.charge", "100"},
        {"input.voltage", "120"}
        // Intentionally missing many optional fields
    };

    UpsData data = UpsData::fromNutVariables("minimal_ups", vars);

    EXPECT_TRUE(data.battery_charge.has_value());
    EXPECT_TRUE(data.input_voltage.has_value());
    EXPECT_FALSE(data.battery_runtime.has_value());
    EXPECT_FALSE(data.ups_status.has_value());
}

TEST_F(UpsDataTest, ParseInvalidNumericValue) {
    UpsData data;
    data.device_id = "test_ups";

    // Should handle gracefully without crashing
    data.updateFieldFromMqtt("battery_charge", "not_a_number");

    // The field should either be unset or set to a default
    // Implementation-dependent behavior
}

TEST_F(UpsDataTest, RuntimeConversionFromSeconds) {
    std::map<std::string, std::string> vars = {
        {"battery.runtime", "3600"}  // 1 hour in seconds
    };

    UpsData data = UpsData::fromNutVariables("test_ups", vars);

    EXPECT_TRUE(data.battery_runtime.has_value());
    EXPECT_EQ(data.battery_runtime.value(), 3600);
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
