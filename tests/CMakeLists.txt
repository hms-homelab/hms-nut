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

# Enable testing
enable_testing()

# Add tests
add_test(NAME DeviceMapperTests COMMAND test_device_mapper)
add_test(NAME UpsDataTests COMMAND test_ups_data)

# Print configuration
message(STATUS "==================== HMS-NUT Tests Configuration ====================")
message(STATUS "GoogleTest: ${GTEST_BOTH_LIBRARIES}")
message(STATUS "=====================================================================")
