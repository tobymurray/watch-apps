# Host tests for HapticLab, built out of tree like the app.
#
# Three targets, split on what each one is evidence about:
#
#   hapticlab-pure-tests:   the candidate set, the trial plan, the protocol
#                              state machine, the row formats, the frame
#                              builder and the analysis arithmetic. No SDK, no
#                              kernel, no filesystem. Everything that decides
#                              what gets played, what gets recorded and what
#                              the answer is lives here, and none of it should
#                              need a watch to check.
#   hapticlab-run-tests:    the run directory and the config file, which go
#                              through SDK::Kernel, the JSON reader and the
#                              SDK's own filesystem test doubles. A correct
#                              trial that never reaches a file looks exactly
#                              like no session at all.
#   hapticlab-analyse:      not a test. The tool that turns a run directory
#                              into the report. It is built here because it
#                              links the same analysis headers the tests hold,
#                              so there is one implementation of the statistics
#                              rather than two that have to agree. A ctest
#                              drives it over a synthetic run end to end.
#
# The renderer's own tests live in its crate (cargo test --features std); they
# cover the round panel and the instruction wrapping, which no test here can
# reach.

cmake_minimum_required(VERSION 3.21)
project(hapticlab-host-tests CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra)
endif()

set(APP_LIBS "${CMAKE_CURRENT_LIST_DIR}/../Software/Libs")
set(APP_GUI "${CMAKE_CURRENT_LIST_DIR}/../Software/Apps/CustomGUI")

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

# ---------------------------------------------------------------------------
# Everything that needs no kernel
#
# The headers under Software/Libs/Header are header-only and free of SDK types,
# which is the property that makes this target worth having: if it ever needs
# the SDK to build, something has leaked into the files that decide what gets
# played and what the answer is. The one exception is haptic_lab_gui.h, which
# is plain C.
# ---------------------------------------------------------------------------

add_executable(hapticlab-pure-tests
    Patterns_test.cpp
    Schedule_test.cpp
    Protocol_test.cpp
    FrameBuilder_test.cpp
    Csv_test.cpp
    SweepStats_test.cpp
    Confusion_test.cpp
)
target_include_directories(hapticlab-pure-tests PRIVATE
    "${APP_LIBS}/Header"
    "${APP_GUI}"
)
target_link_libraries(hapticlab-pure-tests PRIVATE GTest::gtest_main)
add_test(NAME hapticlab-pure-tests COMMAND hapticlab-pure-tests)

# ---------------------------------------------------------------------------
# The run directory and the config file
#
# coreJSON is a submodule in the SDK checkout, so this target is skipped rather
# than failing configure when it has not been initialised; the pure tests are
# still worth running on a machine that has not.
# ---------------------------------------------------------------------------

set(COREJSON_DIR "${UNA_SDK_ROOT}/ThirdParty/coreJSON/source")

if(EXISTS "${COREJSON_DIR}/core_json.c")
    add_executable(hapticlab-run-tests
        Recorder_test.cpp
        SessionConfig_test.cpp
        "${APP_LIBS}/Sources/Recorder.cpp"
        "${UNA_SDK_ROOT}/Libs/Source/JSON/JsonStreamReader.cpp"
        "${COREJSON_DIR}/core_json.c"
        "${UNA_SDK_ROOT}/Tests/Host/support/KernelTestDoubles.cpp"
        "${UNA_SDK_ROOT}/Libs/Source/UnaLogger/Logger.cpp"
        "${UNA_SDK_ROOT}/Libs/Source/Wrappers/StdLibWrappers.c"
    )
    target_include_directories(hapticlab-run-tests PRIVATE
        "${APP_LIBS}/Header"
        "${APP_GUI}"
        "${COREJSON_DIR}/include"
        "${UNA_SDK_ROOT}/Libs/Header"
        "${UNA_SDK_ROOT}/Tests/Host/support"
    )
    target_link_libraries(hapticlab-run-tests PRIVATE GTest::gtest_main)
    add_test(NAME hapticlab-run-tests COMMAND hapticlab-run-tests)
else()
    message(STATUS "HapticLab: no coreJSON submodule, skipping hapticlab-run-tests")
endif()

# ---------------------------------------------------------------------------
# The report generator
#
# The round trip is what earns it a ctest: a synthetic run is written, the real
# tool reads it, and the report has to come out with the numbers the fixture
# put in. Without that, "the analysis works" would rest on nobody having
# changed a column name.
# ---------------------------------------------------------------------------

add_executable(hapticlab-analyse analyse.cpp)
target_include_directories(hapticlab-analyse PRIVATE "${APP_LIBS}/Header")

add_test(NAME hapticlab-report-roundtrip
    COMMAND "${CMAKE_COMMAND}"
        -DANALYSE=$<TARGET_FILE:hapticlab-analyse>
        -DFIXTURES=${CMAKE_CURRENT_LIST_DIR}/fixtures
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/report-roundtrip
        -P "${CMAKE_CURRENT_LIST_DIR}/cmake/ReportRoundtrip.cmake"
)
