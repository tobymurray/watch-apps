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
# Tier 1: the recorder's storage and settings
#
# These write through SDK::Kernel, so they need the kernel doubles. Settings
# also needs the SDK's JSON reader, which is built on coreJSON -- a submodule,
# so the whole target is skipped when it has not been initialized rather than
# failing configure.
# ---------------------------------------------------------------------------

# Both round-trip tests below run the real host scripts, which need python3.
# Skipped, not failed, without it -- the C++ halves still run.
find_package(Python3 COMPONENTS Interpreter QUIET)

set(COREJSON_DIR "${UNA_SDK_ROOT}/ThirdParty/coreJSON/source")

if(EXISTS "${COREJSON_DIR}/core_json.c")
    add_executable(sleeplab-store-tests
        Settings_test.cpp
        NightStore_test.cpp
        "${APP_LIBS}/Sources/Settings.cpp"
        "${APP_LIBS}/Sources/NightStore.cpp"
        # The storage layer sits on top of the engine: settings validate a
        # bedtime window with Engine::inWindow, and the index is what the
        # baseline is rebuilt from.
        "${APP_LIBS}/Sources/Engine/NightSegmenter.cpp"
        "${APP_LIBS}/Sources/Engine/BaselineStore.cpp"
        "${UNA_SDK_ROOT}/Libs/Source/JSON/JsonStreamReader.cpp"
        "${UNA_SDK_ROOT}/Libs/Source/JSON/JsonStreamWriter.cpp"
        "${COREJSON_DIR}/core_json.c"
        ${SDK_DOUBLES}
    )
    target_include_directories(sleeplab-store-tests PRIVATE
        "${APP_LIBS}/Header"
        "${COREJSON_DIR}/include"
        ${SDK_DOUBLE_INCLUDES}
    )
    target_link_libraries(sleeplab-store-tests PRIVATE GTest::gtest_main)
    add_test(NAME sleeplab-store-tests COMMAND sleeplab-store-tests)
    # Not a test: writes real nights with the real writer so the real host
    # script can be run against them. Same two-level check as the probe's.
    add_executable(night-log-export
        night_log_export.cpp
        "${APP_LIBS}/Sources/NightStore.cpp"
        "${APP_LIBS}/Sources/Engine/BaselineStore.cpp"
        "${UNA_SDK_ROOT}/Libs/Source/JSON/JsonStreamReader.cpp"
        "${UNA_SDK_ROOT}/Libs/Source/JSON/JsonStreamWriter.cpp"
        "${COREJSON_DIR}/core_json.c"
        ${SDK_DOUBLES}
    )
    target_include_directories(night-log-export PRIVATE
        "${APP_LIBS}/Header"
        "${COREJSON_DIR}/include"
        ${SDK_DOUBLE_INCLUDES}
    )

    if(Python3_Interpreter_FOUND)
        add_test(NAME sleeplab-night-report-roundtrip
                 COMMAND ${CMAKE_COMMAND}
                         -DEXPORT=$<TARGET_FILE:night-log-export>
                         -DPYTHON=${Python3_EXECUTABLE}
                         -DSCRIPT=${CMAKE_CURRENT_LIST_DIR}/../Tools/night_report.py
                         -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}/nights
                         -P ${CMAKE_CURRENT_LIST_DIR}/cmake/RunNightReportRoundtrip.cmake)
    endif()
else()
    message(STATUS "SleepLab: no coreJSON submodule, skipping sleeplab-store-tests")
endif()

# ---------------------------------------------------------------------------
# Tier 1b: the whole pipeline, offline
#
# The one target that exercises Service itself -- sample to file to summary --
# by scripting the kernel message queue rather than by replacing the loop. See
# NightHarness.hpp for why that choice, and for what a generated night is and
# is not evidence about. Needs the SDK's SensorConnection because the service
# resolves nine real sensor handles through it.
#
# TZ=UTC is set on the test, not assumed: every time-of-day assertion in the
# suite -- the bedtime window, the alarm, the reported asleep/woke times -- is
# meaningless if the machine running it happens to be in another zone.
# ---------------------------------------------------------------------------

if(EXISTS "${COREJSON_DIR}/core_json.c")
    file(GLOB SLEEPLAB_ENGINE_SOURCES "${APP_LIBS}/Sources/Engine/*.cpp")
    add_executable(sleeplab-pipeline-tests
        Pipeline_test.cpp
        "${APP_LIBS}/Sources/Service.cpp"
        "${APP_LIBS}/Sources/NightStore.cpp"
        "${APP_LIBS}/Sources/RawRecorder.cpp"
        "${APP_LIBS}/Sources/Settings.cpp"
        "${APP_LIBS}/Sources/WallClock.cpp"
        "${APP_LIBS}/Sources/Diag.cpp"
        ${SLEEPLAB_ENGINE_SOURCES}
        "${UNA_SDK_ROOT}/Libs/Source/SensorLayer/SensorConnection.cpp"
        "${UNA_SDK_ROOT}/Libs/Source/JSON/JsonStreamReader.cpp"
        "${UNA_SDK_ROOT}/Libs/Source/JSON/JsonStreamWriter.cpp"
        "${COREJSON_DIR}/core_json.c"
        ${SDK_DOUBLES}
    )
    target_include_directories(sleeplab-pipeline-tests PRIVATE
        "${APP_LIBS}/Header"
        "${COREJSON_DIR}/include"
        "${CMAKE_CURRENT_LIST_DIR}"
        ${SDK_DOUBLE_INCLUDES}
    )
    target_link_libraries(sleeplab-pipeline-tests PRIVATE GTest::gtest_main)
    add_test(NAME sleeplab-pipeline-tests COMMAND sleeplab-pipeline-tests)
    set_tests_properties(sleeplab-pipeline-tests PROPERTIES ENVIRONMENT "TZ=UTC")
else()
    message(STATUS "SleepLab: no coreJSON submodule, skipping sleeplab-pipeline-tests")
endif()

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
# thing CI checks rather than a thing the README claims.
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
