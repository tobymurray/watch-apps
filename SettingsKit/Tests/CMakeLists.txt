# Host tests for SettingsKit, built out of tree like the apps that use it.
#
# The splice is the only part of the write path that runs without a kernel, and
# it decides which bytes of a real settings file get rewritten. The addresses,
# the File primitives and the commit rename are exercised by nothing but a watch.

cmake_minimum_required(VERSION 3.21)
project(settingskit-host-tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    # -Wno-c++20-extensions: a Field is written with designated initialisers on
    # purpose, which C++17 accepts as an extension and -Wpedantic reports. The
    # app targets that compile the same descriptors do not use -Wpedantic, and a
    # wall of warnings here would hide a real one.
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-c++20-extensions)
endif()

set(SETTINGSKIT_HEADER "${CMAKE_CURRENT_LIST_DIR}/../Header")

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

add_executable(settingskit-splice-tests SettingsSplice_test.cpp)
target_include_directories(settingskit-splice-tests PRIVATE "${SETTINGSKIT_HEADER}")
target_link_libraries(settingskit-splice-tests PRIVATE GTest::gtest_main)
add_test(NAME settingskit-splice-tests COMMAND settingskit-splice-tests)
