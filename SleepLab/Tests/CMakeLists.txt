# Host tests for SleepLab, built out of tree like the app.
#
# Two rules shape this file, both inherited from the rest of the repo:
#
#   * The engine (Tier 2) includes no SDK header at all, so its tests build
#     against nothing but GoogleTest and run anywhere.
#   * Anything that writes through SDK::Kernel needs the kernel test doubles,
#     which are taken from the SDK checkout rather than duplicated here -- so
#     a failure means the same thing it means in the SDK's own host tests.
#
# What is deliberately NOT here: any test that enumerates a directory. The
# stock `InMemoryFileSystem` ships `EmptyDirectory`, a stub that always reports
# no entries; the real enumerating fake (`InMemoryDirectory`) exists only on
# `una-sdk`'s `poc/athensrun` branch. Rather than require a particular SDK
# branch to run the suite, nothing on the tested path enumerates: the history
# reader probes the filenames it can derive from a date instead of scanning
# the folder, which is also more robust on hardware. See Tests/README.md.

cmake_minimum_required(VERSION 3.21)
project(sleeplab-host-tests CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(APP_LIBS   "${CMAKE_CURRENT_LIST_DIR}/../Software/Libs")
set(PROBE_LIBS "${CMAKE_CURRENT_LIST_DIR}/../Probe/Software/Libs")

if("$ENV{UNA_SDK}" STREQUAL "")
    message(FATAL_ERROR
        "UNA_SDK environment variable is not set. Set UNA_SDK to the SDK root; "
        "the tests take GoogleTest and the kernel test doubles from there."
    )
endif()
set(UNA_SDK_ROOT "$ENV{UNA_SDK}")

# The SDK's pinned GoogleTest revision rather than one chosen here.
include("${UNA_SDK_ROOT}/Tests/Host/cmake/FetchGoogleTest.cmake")

enable_testing()

set(SDK_DOUBLES
    "${UNA_SDK_ROOT}/Tests/Host/support/KernelTestDoubles.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/UnaLogger/Logger.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/Wrappers/StdLibWrappers.c"
)
set(SDK_DOUBLE_INCLUDES
    "${UNA_SDK_ROOT}/Libs/Header"
    "${UNA_SDK_ROOT}/Tests/Host/support"
)

# ---------------------------------------------------------------------------
# Tier 2: the engine
#
# Zero SDK headers by construction, so this target links against GoogleTest and
# nothing else. That is not tidiness: it is what lets every sleep-science
# decision in the app be argued about at a desk, against fixtures whose answers
# are known, with no simulator and no hardware in the loop.
# ---------------------------------------------------------------------------

add_executable(sleeplab-engine-tests
    EpochCounter_test.cpp
    SleepWakeScorer_test.cpp
    WornGate_test.cpp
    NightSummary_test.cpp
    NightSegmenter_test.cpp
    BaselineStore_test.cpp
    RestfulnessBand_test.cpp
    "${APP_LIBS}/Sources/Engine/EpochCounter.cpp"
    "${APP_LIBS}/Sources/Engine/SleepWakeScorer.cpp"
    "${APP_LIBS}/Sources/Engine/WornGate.cpp"
    "${APP_LIBS}/Sources/Engine/NightSummary.cpp"
    "${APP_LIBS}/Sources/Engine/NightSegmenter.cpp"
    "${APP_LIBS}/Sources/Engine/BaselineStore.cpp"
    "${APP_LIBS}/Sources/Engine/RestfulnessBand.cpp"
)
target_include_directories(sleeplab-engine-tests PRIVATE
    "${APP_LIBS}/Header"
    "${CMAKE_CURRENT_LIST_DIR}"
)
target_link_libraries(sleeplab-engine-tests PRIVATE GTest::gtest_main)
add_test(NAME sleeplab-engine-tests COMMAND sleeplab-engine-tests)

# ---------------------------------------------------------------------------
# Tier 0: the probe's on-disk record
# ---------------------------------------------------------------------------

add_executable(sleeplab-probe-tests
    ProbeLog_test.cpp
    "${PROBE_LIBS}/Sources/ProbeLog.cpp"
    ${SDK_DOUBLES}
)
target_include_directories(sleeplab-probe-tests PRIVATE
    "${PROBE_LIBS}/Header"
    ${SDK_DOUBLE_INCLUDES}
)
target_link_libraries(sleeplab-probe-tests PRIVATE GTest::gtest_main)
add_test(NAME sleeplab-probe-tests COMMAND sleeplab-probe-tests)

# Not a test: it writes a real log with the real writer so the real host
# script can be run against it. The tests above pin the writer to this repo's
# reading of the format; this pins it to the reader that actually consumes it.
add_executable(probe-log-export
    probe_log_export.cpp
    "${PROBE_LIBS}/Sources/ProbeLog.cpp"
    ${SDK_DOUBLES}
)
target_include_directories(probe-log-export PRIVATE
    "${PROBE_LIBS}/Header"
    ${SDK_DOUBLE_INCLUDES}
)

# Run the export through the real script, so "the format is a contract" is a
# thing CI checks rather than a thing the README claims. Skipped, not failed,
# when there is no python3 -- the C++ half still runs.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME sleeplab-probe-report-roundtrip
             COMMAND ${CMAKE_COMMAND}
                     -DEXPORT=$<TARGET_FILE:probe-log-export>
                     -DPYTHON=${Python3_EXECUTABLE}
                     -DSCRIPT=${CMAKE_CURRENT_LIST_DIR}/../Tools/probe_report.py
                     -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}/roundtrip
                     -P ${CMAKE_CURRENT_LIST_DIR}/cmake/RunReportRoundtrip.cmake)
else()
    message(STATUS "SleepLab: no python3, skipping the probe-report round trip")
endif()
