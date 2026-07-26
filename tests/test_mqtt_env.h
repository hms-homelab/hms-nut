#pragma once

#include <cstdlib>
#include <string>

/*
 * Broker details for the integration tests.
 *
 * Everything comes from the environment. Nothing here carries a real host or
 * credential, so this file is safe to commit to a public repo. Point the tests
 * at a broker with:
 *
 *   MQTT_BROKER=192.168.x.x MQTT_USER=someuser MQTT_PASSWORD=secret ./run_tests
 *
 * Tests that cannot reach a broker are expected to GTEST_SKIP().
 */

inline std::string mqtt_test_broker()
{
    const char *v = std::getenv("MQTT_BROKER");
    return (v && *v) ? v : "127.0.0.1";
}

inline std::string mqtt_test_url()
{
    return "tcp://" + mqtt_test_broker() + ":1883";
}

inline std::string mqtt_test_user()
{
    const char *v = std::getenv("MQTT_USER");
    if (!v || !*v) v = std::getenv("MQTT_USERNAME");
    return (v && *v) ? v : "";
}

inline std::string mqtt_test_password()
{
    const char *v = std::getenv("MQTT_PASSWORD");
    return (v && *v) ? v : "";
}
