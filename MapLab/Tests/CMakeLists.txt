# Host tests for MapLab's pure core -- the rasteriser, the palette, the draft
# wire format and the timing harness.
#
# One executable, and it links no SDK at all. That is the point: everything
# under test here is arithmetic and bytes, and keeping it buildable with a
# plain compiler is what keeps it that way. The parts that touch the kernel
# (BenchSuite, BenchLog) are not tested here -- they are thin wrappers around
# the filesystem whose whole content is the thing being measured, and a fake
# filesystem would time the fake.
#
#   export UNA_SDK=/path/to/una-sdk        # only for GoogleTest, see below
#   cd MapLab/Tests
#   cmake -B build . && cmake --build build && (cd build && ctest --output-on-failure)

cmake_minimum_required(VERSION 3.21)
project(maplab-host-tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(MAPLAB "${CMAKE_CURRENT_LIST_DIR}/../Software/Libs")

if("$ENV{UNA_SDK}" STREQUAL "")
    message(FATAL_ERROR
        "UNA_SDK environment variable is not set. Set UNA_SDK to the SDK root; "
        "these tests take only GoogleTest from it, at the SDK's own pinned "
        "revision, so a failure here means what it means in the SDK's tests."
    )
endif()
include("$ENV{UNA_SDK}/Tests/Host/cmake/FetchGoogleTest.cmake")

enable_testing()

add_executable(maplab-tests
    Canvas_test.cpp
    Palette_test.cpp
    VecScene_test.cpp
    SceneRender_test.cpp
    Bench_test.cpp
    Cards_test.cpp
    "${MAPLAB}/Sources/Canvas.cpp"
    "${MAPLAB}/Sources/Palette.cpp"
    "${MAPLAB}/Sources/VecScene.cpp"
    "${MAPLAB}/Sources/SceneRender.cpp"
    "${MAPLAB}/Sources/Cards.cpp"
)
target_include_directories(maplab-tests PRIVATE "${MAPLAB}/Header")
target_link_libraries(maplab-tests PRIVATE GTest::gtest_main)
add_test(NAME maplab-tests COMMAND maplab-tests)
