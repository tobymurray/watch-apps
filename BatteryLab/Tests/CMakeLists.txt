# Host tests for BatteryLab's SDK-free code: the raw log's rows and the file they go to.

cmake_minimum_required(VERSION 3.21)
project(batterylab-host-tests C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Wpedantic)

if("$ENV{UNA_SDK}" STREQUAL "")
    message(FATAL_ERROR "UNA_SDK environment variable is not set. Set UNA_SDK to the SDK root.")
endif()
set(UNA_SDK_ROOT "$ENV{UNA_SDK}")

include("${UNA_SDK_ROOT}/Tests/Host/cmake/FetchGoogleTest.cmake")

enable_testing()

add_executable(batterylab-raw-tests
    RawLog_test.cpp
)
target_include_directories(batterylab-raw-tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../Software/Libs/Header"
    "${UNA_SDK_ROOT}/Libs/Header"
    "${UNA_SDK_ROOT}/Tests/Host/support"
)
target_link_libraries(batterylab-raw-tests PRIVATE GTest::gtest_main)
add_test(NAME batterylab-raw-tests COMMAND batterylab-raw-tests)
