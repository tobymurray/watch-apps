# Host tests for SensorLab, built out of tree like the app.
#
# Three rules shape this file, all inherited from the rest of the repo:
#
#   * The evidence core (Tier 0) includes no SDK header at all, so its tests
#     build against nothing but GoogleTest and run anywhere. That is not
#     tidiness: it is what lets every verdict rule and every statistic be
#     argued about at a desk, against fixtures whose answers are known, with no
#     simulator and no hardware in the loop.
#   * Anything that writes through SDK::Kernel takes the kernel test doubles
#     from the SDK checkout rather than duplicating them here -- so a failure
#     means the same thing it means in the SDK's own host tests.
#   * `Tools/gen_catalogue.py --check` runs as a test, because the generated
#     type table is committed and can therefore fall behind the headers it was
#     generated from. That is exactly what `Docs/SensorsLayer.md` did, and it is
#     the reason the table is generated in the first place.
#
# What is deliberately NOT here: any test that enumerates a directory. The
# stock `InMemoryFileSystem` ships `EmptyDirectory`, a stub that always reports
# no entries; the real enumerating fake (`InMemoryDirectory`) exists only on
# `una-sdk`'s `poc/athensrun` branch. Rather than require a particular SDK
# branch to run the suite, nothing on the tested path enumerates: the app
# derives every filename it reads from a run id or a firmware string rather
# than scanning a folder, which is also more robust on hardware. See README.md.

cmake_minimum_required(VERSION 3.21)
project(sensorlab-host-tests CXX C)

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

# The app's version, so a manifest written in a test carries the same field a
# packed .uapp would. Overridable for a one-off.
if(NOT DEFINED BUILD_VERSION)
    set(BUILD_VERSION "0.1.0")
endif()

find_package(Python3 COMPONENTS Interpreter QUIET)

# ---------------------------------------------------------------------------
# Tier 0: the evidence core
#
# Zero SDK headers by construction. The catalogue, the claim store, the verdict
# rules, the completeness arithmetic, the histograms and every per-field
# statistic -- all of it links against GoogleTest and nothing else.
#
# These tests are the reason to believe any number the app ever prints.
# ---------------------------------------------------------------------------

add_executable(sensorlab-core-tests
    Stats_test.cpp
    Evidence_test.cpp
    CatalogueGeneration_test.cpp
    "${APP_LIBS}/Sources/Catalogue.cpp"
    "${APP_LIBS}/Sources/Evidence/ClaimStore.cpp"
    "${APP_LIBS}/Sources/Stats/StreamStats.cpp"
)
target_include_directories(sensorlab-core-tests PRIVATE
    "${APP_LIBS}/Header"
    "${CMAKE_CURRENT_LIST_DIR}"
)
target_link_libraries(sensorlab-core-tests PRIVATE GTest::gtest_main)
add_test(NAME sensorlab-core-tests COMMAND sensorlab-core-tests)

# ---------------------------------------------------------------------------
# The generated table is current
#
# `SensorTypeTable.generated.hpp` is committed, so it can drift from the SDK it
# was generated from. This is the alarm. It fails loudly on a new sensor type,
# a renamed parser, a changed field count or a parser appearing for one of the
# five types that ship none -- every one of which is news rather than noise.
# ---------------------------------------------------------------------------

if(Python3_Interpreter_FOUND)
    add_test(NAME sensorlab-catalogue-current
             COMMAND ${Python3_EXECUTABLE}
                     "${CMAKE_CURRENT_LIST_DIR}/../Tools/gen_catalogue.py"
                     --sdk "${UNA_SDK_ROOT}"
                     --check
                     --out "${APP_LIBS}/Header/Catalogue/SensorTypeTable.generated.hpp")
else()
    message(STATUS "SensorLab: no python3, skipping the generated-table check")
endif()

# ---------------------------------------------------------------------------
# Tier 1: the whole pipeline, offline
#
# The one target that exercises `Service` itself -- handshake to sample to file
# to profile -- by scripting the kernel message queue rather than by replacing
# the loop. See RunHarness.hpp for why that choice, and for what a generated
# run is and is not evidence about.
#
# TZ=UTC is set on the test rather than assumed: the manifest carries a wall
# clock and a report renders times of day from it.
# ---------------------------------------------------------------------------

set(COREJSON_DIR "${UNA_SDK_ROOT}/ThirdParty/coreJSON/source")

if(EXISTS "${COREJSON_DIR}/core_json.c")
    file(GLOB SENSORLAB_SOURCES
        "${APP_LIBS}/Sources/*.cpp"
        "${APP_LIBS}/Sources/Evidence/*.cpp"
        "${APP_LIBS}/Sources/Probes/*.cpp"
        "${APP_LIBS}/Sources/Profile/*.cpp"
        "${APP_LIBS}/Sources/Stats/*.cpp"
    )
    add_executable(sensorlab-pipeline-tests
        Pipeline_test.cpp
        ${SENSORLAB_SOURCES}
        "${UNA_SDK_ROOT}/Libs/Source/JSON/JsonStreamReader.cpp"
        "${UNA_SDK_ROOT}/Libs/Source/JSON/JsonStreamWriter.cpp"
        "${COREJSON_DIR}/core_json.c"
        ${SDK_DOUBLES}
    )
    target_include_directories(sensorlab-pipeline-tests PRIVATE
        "${APP_LIBS}/Header"
        "${COREJSON_DIR}/include"
        "${CMAKE_CURRENT_LIST_DIR}"
        ${SDK_DOUBLE_INCLUDES}
    )
    target_compile_definitions(sensorlab-pipeline-tests PRIVATE
        SENSORLAB_VERSION="${BUILD_VERSION}")
    target_link_libraries(sensorlab-pipeline-tests PRIVATE GTest::gtest_main)
    add_test(NAME sensorlab-pipeline-tests COMMAND sensorlab-pipeline-tests)
    set_tests_properties(sensorlab-pipeline-tests PROPERTIES ENVIRONMENT "TZ=UTC")

    # Not a test: it writes a real profile and a real run log with the real
    # writers, so the real host scripts can be run against them. The tests above
    # pin the writers to this repository's reading of the format; this pins them
    # to the readers that actually consume it.
    add_executable(profile-export
        profile_export.cpp
        ${SENSORLAB_SOURCES}
        "${UNA_SDK_ROOT}/Libs/Source/JSON/JsonStreamReader.cpp"
        "${UNA_SDK_ROOT}/Libs/Source/JSON/JsonStreamWriter.cpp"
        "${COREJSON_DIR}/core_json.c"
        ${SDK_DOUBLES}
    )
    target_include_directories(profile-export PRIVATE
        "${APP_LIBS}/Header"
        "${COREJSON_DIR}/include"
        "${CMAKE_CURRENT_LIST_DIR}"
        ${SDK_DOUBLE_INCLUDES}
    )
    target_compile_definitions(profile-export PRIVATE
        SENSORLAB_VERSION="${BUILD_VERSION}")

    if(Python3_Interpreter_FOUND)
        # Round-trip: export -> profile_report.py -> profile_diff.py. "The format
        # is a contract" becomes a thing CI checks rather than a thing the README
        # claims.
        add_test(NAME sensorlab-report-roundtrip
                 COMMAND ${CMAKE_COMMAND}
                         -DEXPORT=$<TARGET_FILE:profile-export>
                         -DPYTHON=${Python3_EXECUTABLE}
                         -DREPORT=${CMAKE_CURRENT_LIST_DIR}/../Tools/profile_report.py
                         -DDIFF=${CMAKE_CURRENT_LIST_DIR}/../Tools/profile_diff.py
                         -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}/roundtrip
                         -P ${CMAKE_CURRENT_LIST_DIR}/cmake/RunReportRoundtrip.cmake)
    else()
        message(STATUS "SensorLab: no python3, skipping the report round trip")
    endif()
else()
    message(STATUS "SensorLab: no coreJSON submodule, skipping the pipeline tests")
endif()
