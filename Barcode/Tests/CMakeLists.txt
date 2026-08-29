# Host tests for Barcode, built out of tree like the app.
#
# Two executables, split on what each one is evidence about rather than on what
# it covers:
#
#   barcode-pure-tests     -- the Code 128 encoder and its 107-row spec table,
#                             and the display geometry. No SDK, no kernel, no
#                             filesystem. The encoder is where a wrong barcode
#                             would come from, and the geometry is where an
#                             unscannable one would; both are arguable at a desk
#                             and neither should need a watch to check.
#   barcode-service-tests  -- reading input.json into a published state, which
#                             goes through SDK::AppConfig, SDK::Kernel and
#                             coreJSON, plus the real Service driven by a
#                             scripted message queue.
#
# The second target is the one that earns its keep the way SunGlance's glance
# tests do: a correct id that never reaches the GUI looks exactly like no file
# at all, and nothing in a unit test sees the difference.

cmake_minimum_required(VERSION 3.21)
project(barcode-host-tests CXX C)

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

# ---------------------------------------------------------------------------
# The encoder and the geometry
#
# Code128.hpp, Qr.hpp and BarcodeLayout.hpp are all header-only and depend on
# nothing, which is the property that makes this target worth having: if it
# ever needs the SDK to build, something has leaked into the files that decide
# whether a barcode is right and whether it can be read.
# ---------------------------------------------------------------------------

add_executable(barcode-pure-tests
    Code128_test.cpp
    Itf_test.cpp
    Qr_test.cpp
    Layout_test.cpp
)
target_include_directories(barcode-pure-tests PRIVATE "${APP_LIBS}/Header")
target_link_libraries(barcode-pure-tests PRIVATE GTest::gtest_main)
add_test(NAME barcode-pure-tests COMMAND barcode-pure-tests)

# ---------------------------------------------------------------------------
# The reader and the service
#
# coreJSON is a submodule in the SDK checkout, so this target is skipped rather
# than failing configure when it has not been initialised -- the pure tests are
# still worth running on a machine that has not.
# ---------------------------------------------------------------------------

set(COREJSON_DIR "${UNA_SDK_ROOT}/ThirdParty/coreJSON/source")

if(EXISTS "${COREJSON_DIR}/core_json.c")
    add_executable(barcode-service-tests
        Service_test.cpp
        "${APP_LIBS}/Sources/AppConfigFields.cpp"
        "${APP_LIBS}/Sources/Service.cpp"
        "${UNA_SDK_ROOT}/Libs/Source/AppConfig/AppConfig.cpp"
        "${UNA_SDK_ROOT}/Libs/Source/JSON/JsonStreamReader.cpp"
        "${COREJSON_DIR}/core_json.c"
        "${UNA_SDK_ROOT}/Tests/Host/support/KernelTestDoubles.cpp"
        "${UNA_SDK_ROOT}/Libs/Source/UnaLogger/Logger.cpp"
        "${UNA_SDK_ROOT}/Libs/Source/Wrappers/StdLibWrappers.c"
    )
    target_include_directories(barcode-service-tests PRIVATE
        "${APP_LIBS}/Header"
        "${COREJSON_DIR}/include"
        "${UNA_SDK_ROOT}/Libs/Header"
        "${UNA_SDK_ROOT}/Tests/Host/support"
        "${CMAKE_CURRENT_LIST_DIR}"
    )
    target_link_libraries(barcode-service-tests PRIVATE GTest::gtest_main)
    add_test(NAME barcode-service-tests COMMAND barcode-service-tests)
else()
    message(STATUS "Barcode: no coreJSON submodule, skipping barcode-service-tests")
endif()
