# Host tests for NotifyToggle, built out of tree like the app.
#
# The splice is the only part of the write path that runs without a kernel, and
# it decides which bytes of a real settings file get rewritten. The addresses,
# the File primitives and the commit rename are exercised by nothing but a watch.

cmake_minimum_required(VERSION 3.21)
project(notifytoggle-host-tests CXX)

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

# The SDK's pinned GoogleTest revision rather than one chosen here, so a failure
# means the same thing it means in the SDK's own host tests.
include("${UNA_SDK_ROOT}/Tests/Host/cmake/FetchGoogleTest.cmake")

enable_testing()

add_executable(notifytoggle-splice-tests SettingsSplice_test.cpp)
target_include_directories(notifytoggle-splice-tests PRIVATE "${APP_LIBS}/Header")
target_link_libraries(notifytoggle-splice-tests PRIVATE GTest::gtest_main)
add_test(NAME notifytoggle-splice-tests COMMAND notifytoggle-splice-tests)
