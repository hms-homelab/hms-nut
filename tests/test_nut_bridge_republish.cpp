#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "services/NutBridgeService.h"
#include "mqtt/MqttClient.h"
#include "mqtt/DiscoveryPublisher.h"

using namespace hms_nut;
using ::testing::Return;
using ::testing::_;
using ::testing::AtLeast;

// Mock MqttClient for testing
class MockMqttClient : public MqttClient {
public:
    MockMqttClient() : MqttClient("test_client") {}

    // Override to make it mockable (would need virtual methods in real implementation)
    // For now, we'll test via integration with real client setup
};

// Test fixture for NutBridgeService republish tests
class NutBridgeRepublishTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create MQTT client
        mqtt_client_ = std::make_shared<MqttClient>("test_nut_bridge_republish");
    }

    void TearDown() override {
        if (mqtt_client_) {
            mqtt_client_->disconnect();
        }
    }

    std::shared_ptr<MqttClient> mqtt_client_;
};

// Test: Republish should fail when MQTT is not connected
TEST_F(NutBridgeRepublishTest, RepublishFailsWhenMqttNotConnected) {
    // Create NutBridgeService without connecting to MQTT
    NutBridgeService bridge(
        mqtt_client_,
        "localhost",
        3493,
        "test_ups@localhost",
        "test_device",
        "Test UPS Device",
        60
    );

    // Republish should fail because MQTT is not connected
    bool result = bridge.republishDiscovery();
    EXPECT_FALSE(result);
}

// Test: Republish should succeed when MQTT is connected (integration test)
TEST_F(NutBridgeRepublishTest, DISABLED_RepublishSucceedsWhenMqttConnected) {
    // This is an integration test that requires MQTT broker to be running
    // Disabled by default, can be enabled for integration testing

    // Connect to local MQTT broker
    bool connected = mqtt_client_->connect(
        "tcp://192.168.2.15:1883",
        "aamat",
        "exploracion"
    );

    if (!connected) {
        GTEST_SKIP() << "MQTT broker not available for integration test";
    }

    // Create NutBridgeService
    NutBridgeService bridge(
        mqtt_client_,
        "localhost",
        3493,
        "test_ups@localhost",
        "test_device_integ",
        "Test UPS Device Integration",
        60
    );

    // Republish should succeed
    bool result = bridge.republishDiscovery();
    EXPECT_TRUE(result);

    // Clean up - disconnect
    mqtt_client_->disconnect();
}

// Test: Multiple republish calls should all work
TEST_F(NutBridgeRepublishTest, MultipleRepublishCalls) {
    // Create NutBridgeService without connecting to MQTT
    NutBridgeService bridge(
        mqtt_client_,
        "localhost",
        3493,
        "test_ups@localhost",
        "test_device",
        "Test UPS Device",
        60
    );

    // Multiple calls should all fail consistently (not crash)
    EXPECT_FALSE(bridge.republishDiscovery());
    EXPECT_FALSE(bridge.republishDiscovery());
    EXPECT_FALSE(bridge.republishDiscovery());
}

// Test: Republish can be called before service is started
TEST_F(NutBridgeRepublishTest, RepublishBeforeServiceStart) {
    NutBridgeService bridge(
        mqtt_client_,
        "localhost",
        3493,
        "test_ups@localhost",
        "test_device",
        "Test UPS Device",
        60
    );

    // Should not crash even if service hasn't been started
    bool result = bridge.republishDiscovery();
    EXPECT_FALSE(result);  // Should fail because MQTT not connected
}

// Test: Republish state is independent of service running state
TEST_F(NutBridgeRepublishTest, RepublishIndependentOfRunningState) {
    NutBridgeService bridge(
        mqtt_client_,
        "localhost",
        3493,
        "test_ups@localhost",
        "test_device",
        "Test UPS Device",
        60
    );

    // Start the service
    bridge.start();

    // Republish should still fail without MQTT connection
    bool result = bridge.republishDiscovery();
    EXPECT_FALSE(result);

    // Stop the service
    bridge.stop();

    // Republish should still work the same way
    result = bridge.republishDiscovery();
    EXPECT_FALSE(result);
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
