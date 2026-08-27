# Host tests for BacklightProbe's plan and its record, built out of tree like
# the app.
#
# What is testable here is narrow but it is the part that quietly ruins a run:
# the plan's timing (a step that advances before the light has settled produces
# a well-formed sweep of the wrong state) and the exact wording of the results
# file (which is the artefact somebody reads a year later).
#
# The plan tests also assert properties of the *experiment* rather than of the
# code; unique sweep labels, non-zero send timeouts, an auto-off longer than
# the ladder that depends on it. Those are the failures that break nothing, throw
# nothing, and come back with a directory full of files that cannot answer the
# question. See ProbePlan_test.cpp.
#
# What is NOT testable here, and is not pretended to be:
#
#   - That any peripheral base decodes. RegisterSweep is compiled out entirely
#     off ARM, so nothing here reads a register or could tell you a base was
#     wrong. That is a hardware-only claim: see the README.
#   - That the kernel handles REQUEST_BACKLIGHT_SET, or what it does with
#     brightness. The whole point of the app is that nobody knows.
#   - That a bare relative filename resolves into the app's own sandbox.
#
# Deliberately no test target for Service: it blocks on the kernel message queue
# and never returns, exactly as FwDump's does.

cmake_minimum_required(VERSION 3.21)
project(backlightprobe-host-tests CXX C)

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
        "the tests take GoogleTest and the kernel test doubles from there."
    )
endif()
set(UNA_SDK_ROOT "$ENV{UNA_SDK}")

# The SDK's pinned GoogleTest revision rather than one chosen here, so a failure
# means the same thing it means in the SDK's own host tests.
include("${UNA_SDK_ROOT}/Tests/Host/cmake/FetchGoogleTest.cmake")

enable_testing()

add_executable(backlightprobe-tests
    ProbePlan_test.cpp
    ProbeLog_test.cpp
    "${APP_LIBS}/Sources/ProbePlan.cpp"
    "${APP_LIBS}/Sources/ProbeLog.cpp"
    "${UNA_SDK_ROOT}/Tests/Host/support/KernelTestDoubles.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/UnaLogger/Logger.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/Wrappers/StdLibWrappers.c"
)

target_include_directories(backlightprobe-tests PRIVATE
    "${APP_LIBS}/Header"
    "${UNA_SDK_ROOT}/Libs/Header"
    "${UNA_SDK_ROOT}/Tests/Host/support"
)

target_link_libraries(backlightprobe-tests PRIVATE GTest::gtest_main)
add_test(NAME backlightprobe-tests COMMAND backlightprobe-tests)
