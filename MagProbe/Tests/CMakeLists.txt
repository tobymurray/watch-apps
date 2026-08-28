# Host tests for MagProbe, built out of tree like the app.
#
# Everything under `Software/Libs/Header/Mag` and `Fmt.hpp` is deliberately free
# of SDK headers, so it builds against nothing but GoogleTest and can be argued
# about at a desk. That is the whole point: the sensor itself cannot be tested
# anywhere but on the watch (the simulator resolves no sensor drivers for a
# service, SensorLab Docs/FINDINGS.md section 11), so the parts that CAN be
# checked off-device need to be separable from the parts that cannot.
#
# What these tests are not evidence about: whether MAGNETIC_FIELD has a
# producer, what its frame looks like, what its units are, or whether the
# heading points north. Every one of those is a hardware question, and this
# suite is the machine that turns hardware samples into an answer, not the
# answer.

cmake_minimum_required(VERSION 3.21)
project(magprobe-host-tests CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(APP_LIBS "${CMAKE_CURRENT_LIST_DIR}/../Software/Libs")
set(APP_GUI  "${CMAKE_CURRENT_LIST_DIR}/../Software/Apps/CustomGUI")

if("$ENV{UNA_SDK}" STREQUAL "")
    message(FATAL_ERROR
        "UNA_SDK environment variable is not set. Set UNA_SDK to the SDK root; "
        "the tests take GoogleTest from there."
    )
endif()
set(UNA_SDK_ROOT "$ENV{UNA_SDK}")

# The SDK's pinned GoogleTest revision rather than one chosen here.
include("${UNA_SDK_ROOT}/Tests/Host/cmake/FetchGoogleTest.cmake")

enable_testing()

# The pure core: the vector maths, the unit classifier, the frame taxonomy, the
# hard-iron estimator, the integer formatter, and the renderer. No SDK, no
# kernel, no message bus.
add_executable(magprobe-core-tests
    Units_test.cpp
    Frame_test.cpp
    HardIron_test.cpp
    Heading_test.cpp
    Fmt_test.cpp
    Render_test.cpp
    ${APP_GUI}/Canvas.cpp
    ${APP_GUI}/Render.cpp
)
target_include_directories(magprobe-core-tests PRIVATE
    "${APP_LIBS}/Header"
    "${APP_GUI}"
)
target_link_libraries(magprobe-core-tests PRIVATE gtest_main)

include(GoogleTest)
gtest_discover_tests(magprobe-core-tests)

# Renders every screen to a PPM, which is what stands in for the TouchGFX
# simulator this app does not have. Not a test: it produces something to look
# at, and looking is the point.
add_executable(magprobe-screens EXCLUDE_FROM_ALL
    screens_dump.cpp
    ${APP_GUI}/Canvas.cpp
    ${APP_GUI}/Render.cpp
)
target_include_directories(magprobe-screens PRIVATE
    "${APP_LIBS}/Header"
    "${APP_GUI}"
)
