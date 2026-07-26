cmake_minimum_required(VERSION 3.16)
project(hms_nut CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2 -pthread")

# Find required packages
find_package(Drogon CONFIG REQUIRED)
find_package(jsoncpp REQUIRED)
find_package(CURL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# Find Paho MQTT C++ library
find_library(PAHO_MQTTPP3_LIB paho-mqttpp3)
find_library(PAHO_MQTT3AS_LIB paho-mqtt3as)

# Find PostgreSQL C++ library (libpqxx)
find_library(PQXX_LIB pqxx)
find_library(PQ_LIB pq)

# Find NUT client library
find_library(UPSCLIENT_LIB upsclient)

# ── hms-shared modules (via FetchContent) ───────────────────────────────────
include(FetchContent)
FetchContent_Declare(hms_shared
    GIT_REPOSITORY https://github.com/hms-homelab/hms-shared.git
    GIT_TAG        main
)
FetchContent_Populate(hms_shared)

# LLM module (nlohmann_json + curl only)
add_library(hms_llm STATIC
    ${hms_shared_SOURCE_DIR}/llm/src/llm_client.cpp
)
target_include_directories(hms_llm PUBLIC
    ${hms_shared_SOURCE_DIR}/llm/include
)
target_link_libraries(hms_llm PUBLIC
    nlohmann_json::nlohmann_json
    CURL::libcurl
)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Collect all source files
file(GLOB_RECURSE SOURCES
    ${CMAKE_SOURCE_DIR}/src/*.cpp
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Link libraries
target_link_libraries(${PROJECT_NAME}
    Drogon::Drogon
    jsoncpp_lib
    ${PAHO_MQTTPP3_LIB}
    ${PAHO_MQTT3AS_LIB}
    ${PQXX_LIB}
    ${PQ_LIB}
    ${UPSCLIENT_LIB}
    hms_llm
    pthread
    ssl
    crypto
)

# Install target
install(TARGETS ${PROJECT_NAME} DESTINATION /usr/local/bin)

# Print configuration
message(STATUS "==================== HMS-NUT Build Configuration ====================")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Drogon: ${Drogon_VERSION}")
message(STATUS "Paho MQTT C++: ${PAHO_MQTTPP3_LIB}")
message(STATUS "PostgreSQL (libpqxx): ${PQXX_LIB}")
message(STATUS "NUT Client: ${UPSCLIENT_LIB}")
message(STATUS "=====================================================================")
