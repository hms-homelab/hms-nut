cmake_minimum_required(VERSION 3.16)
project(hms_nut_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(GTest REQUIRED)
find_package(jsoncpp REQUIRED)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/../include)
include_directories(${GTEST_INCLUDE_DIRS})

# Find additional libraries needed by the main project
find_library(PQXX_LIB pqxx)
find_library(PQ_LIB pq)

# Collect source files from main project (excluding main.cpp)
set(PROJECT_SOURCES
    ${CMAKE_SOURCE_DIR}/../src/utils/DeviceMapper.cpp
    ${CMAKE_SOURCE_DIR}/../src/nut/UpsData.cpp
)

# DeviceMapper tests
add_executable(test_device_mapper
    test_device_mapper.cpp
    ${CMAKE_SOURCE_DIR}/../src/utils/DeviceMapper.cpp
)
target_link_libraries(test_device_mapper
    GTest::GTest
    GTest::Main
    jsoncpp_lib
    pthread
)
target_include_directories(test_device_mapper PRIVATE ${CMAKE_SOURCE_DIR}/../include)

# UpsData tests
add_executable(test_ups_data
    test_ups_data.cpp
    ${CMAKE_SOURCE_DIR}/../src/nut/UpsData.cpp
)
target_link_libraries(test_ups_data
    GTest::GTest
    GTest::Main
    jsoncpp_lib
    pthread
)
target_include_directories(test_ups_data PRIVATE ${CMAKE_SOURCE_DIR}/../include)

# NutBridge Republish tests
find_library(PAHO_MQTTPP3_LIB paho-mqttpp3)
find_library(PAHO_MQTT3AS_LIB paho-mqtt3as)
find_library(UPSCLIENT_LIB upsclient)
find_package(Drogon REQUIRED)

add_executable(test_nut_bridge_republish
    test_nut_bridge_republish.cpp
    ${CMAKE_SOURCE_DIR}/../src/services/NutBridgeService.cpp
    ${CMAKE_SOURCE_DIR}/../src/mqtt/MqttClient.cpp
    ${CMAKE_SOURCE_DIR}/../src/mqtt/DiscoveryPublisher.cpp
    ${CMAKE_SOURCE_DIR}/../src/nut/NutClient.cpp
    ${CMAKE_SOURCE_DIR}/../src/nut/UpsData.cpp
)
target_link_libraries(test_nut_bridge_republish
    GTest::GTest
    GTest::Main
    gmock
    jsoncpp_lib
    ${PAHO_MQTTPP3_LIB}
    ${PAHO_MQTT3AS_LIB}
    ${UPSCLIENT_LIB}
    pthread
)
target_include_directories(test_nut_bridge_republish PRIVATE ${CMAKE_SOURCE_DIR}/../include)

# Home Assistant Status Subscription tests
add_executable(test_ha_status_subscription
    test_ha_status_subscription.cpp
    ${CMAKE_SOURCE_DIR}/../src/services/NutBridgeService.cpp
    ${CMAKE_SOURCE_DIR}/../src/mqtt/MqttClient.cpp
    ${CMAKE_SOURCE_DIR}/../src/mqtt/DiscoveryPublisher.cpp
    ${CMAKE_SOURCE_DIR}/../src/nut/NutClient.cpp
    ${CMAKE_SOURCE_DIR}/../src/nut/UpsData.cpp
)
target_link_libraries(test_ha_status_subscription
    GTest::GTest
    GTest::Main
    jsoncpp_lib
    ${PAHO_MQTTPP3_LIB}
    ${PAHO_MQTT3AS_LIB}
    ${UPSCLIENT_LIB}
    pthread
)
target_include_directories(test_ha_status_subscription PRIVATE ${CMAKE_SOURCE_DIR}/../include)

# Async Subscriptions tests (verifies HTTP server blocking fix)
add_executable(test_async_subscriptions
    test_async_subscriptions.cpp
    ${CMAKE_SOURCE_DIR}/../src/mqtt/MqttClient.cpp
)
target_link_libraries(test_async_subscriptions
    GTest::GTest
    GTest::Main
    gmock
    jsoncpp_lib
    ${PAHO_MQTTPP3_LIB}
    ${PAHO_MQTT3AS_LIB}
    pthread
)
target_include_directories(test_async_subscriptions PRIVATE ${CMAKE_SOURCE_DIR}/../include)

# HTTP Endpoint tests (requires running service)
find_package(CURL REQUIRED)

add_executable(test_http_endpoints
    test_http_endpoints.cpp
)
target_link_libraries(test_http_endpoints
    GTest::GTest
    GTest::Main
    jsoncpp_lib
    CURL::libcurl
    pthread
)
target_include_directories(test_http_endpoints PRIVATE ${CMAKE_SOURCE_DIR}/../include)

# Enable testing
enable_testing()

# Add tests
add_test(NAME DeviceMapperTests COMMAND test_device_mapper)
add_test(NAME UpsDataTests COMMAND test_ups_data)
add_test(NAME NutBridgeRepublishTests COMMAND test_nut_bridge_republish)
add_test(NAME HAStatusSubscriptionTests COMMAND test_ha_status_subscription)
add_test(NAME AsyncSubscriptionTests COMMAND test_async_subscriptions)
add_test(NAME HTTPEndpointTests COMMAND test_http_endpoints)

# Print configuration
message(STATUS "==================== HMS-NUT Tests Configuration ====================")
message(STATUS "GoogleTest: ${GTEST_BOTH_LIBRARIES}")
message(STATUS "=====================================================================")
