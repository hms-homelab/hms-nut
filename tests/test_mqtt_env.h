#pragma once

#include <cstdlib>
#include <string>

/*
 * Broker details for the integration tests.
 *
 * Everything comes from test-only environment variables. Nothing here carries
 * a real host or credential, so this file is safe to commit to a public repo.
 * Point the tests at a throwaway broker with:
 *
 *   docker run -d --rm --name hmsnut-test-mosq -p 18883:1883 \
 *       eclipse-mosquitto:2 mosquitto -c /mosquitto-no-auth.conf
 *   HMS_NUT_TEST_MQTT_HOST=127.0.0.1 HMS_NUT_TEST_MQTT_PORT=18883 ctest
 *
 * There is deliberately no default host, and the service's own variables
 * (MQTT_BROKER, MQTT_USER, ...) are never read: a test that publishes retained
 * homeassistant/status or discovery messages must never land on a real broker
 * because a shell happened to export the service's settings. With
 * HMS_NUT_TEST_MQTT_HOST unset, every broker test GTEST_SKIP()s.
 */

inline std::string mqtt_test_env(const char *key)
{
    const char *v = std::getenv(key);
    return (v && *v) ? v : "";
}

inline bool mqtt_test_configured()
{
    return !mqtt_test_env("HMS_NUT_TEST_MQTT_HOST").empty();
}

inline std::string mqtt_test_broker()
{
    return mqtt_test_env("HMS_NUT_TEST_MQTT_HOST");
}

inline std::string mqtt_test_port()
{
    std::string port = mqtt_test_env("HMS_NUT_TEST_MQTT_PORT");
    return port.empty() ? "18883" : port;
}

inline std::string mqtt_test_url()
{
    if (!mqtt_test_configured()) return "";
    return "tcp://" + mqtt_test_broker() + ":" + mqtt_test_port();
}

inline std::string mqtt_test_user()
{
    return mqtt_test_env("HMS_NUT_TEST_MQTT_USER");
}

inline std::string mqtt_test_password()
{
    return mqtt_test_env("HMS_NUT_TEST_MQTT_PASSWORD");
}

#define SKIP_WITHOUT_TEST_BROKER()                                            \
    do {                                                                      \
        if (!mqtt_test_configured())                                          \
            GTEST_SKIP() << "HMS_NUT_TEST_MQTT_HOST not set, no test broker"; \
    } while (0)
