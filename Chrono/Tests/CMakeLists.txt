# Host tests for Chrono's stopwatch core, built out of tree like the app.
#
# The core is header-only and knows nothing about the SDK -- it is handed a
# millisecond tick and answers questions about it -- so this needs GoogleTest
# and nothing else. It came out of the SDK's shared `una-sdk-host-tests` binary,
# where an app's tests live outside the app directory and so do not travel with
# it; here they sit next to the app they cover.

cmake_minimum_required(VERSION 3.21)
project(chrono-host-tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(APP_LIBS "${CMAKE_CURRENT_LIST_DIR}/../Software/Libs")

if("$ENV{UNA_SDK}" STREQUAL "")
    message(FATAL_ERROR
        "UNA_SDK environment variable is not set. Set UNA_SDK to the SDK root; "
        "the tests take GoogleTest from there."
    )
endif()
set(UNA_SDK_ROOT "$ENV{UNA_SDK}")

# Take the SDK's pinned GoogleTest revision rather than choosing our own, so a
# failure here means the same thing it means in the SDK's own host tests.
include("${UNA_SDK_ROOT}/Tests/Host/cmake/FetchGoogleTest.cmake")

enable_testing()

add_executable(chrono-stopwatch-tests Stopwatch_test.cpp)

target_include_directories(chrono-stopwatch-tests PRIVATE "${APP_LIBS}/Header")
target_link_libraries(chrono-stopwatch-tests PRIVATE GTest::gtest_main)
add_test(NAME chrono-stopwatch-tests COMMAND chrono-stopwatch-tests)
