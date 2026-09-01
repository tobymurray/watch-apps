# Host tests for what Spin writes, built out of tree like the app.
#
# One executable, covering ActivityWriter: the ride is encoded with the real
# writer against the SDK's in-memory filesystem double and decoded again with
# the SDK's own independent test FIT reader. That reader lives under the SDK's
# Tests/, not its Libs/, so this suite needs an SDK *checkout* rather than an
# SDK install -- which is what $UNA_SDK already is everywhere in this repo.
#
# The renderer's tests are not here. They are Rust unit tests inside the crate
# (`cargo test --features std` in Software/Apps/CustomGUI/rust), the same
# arrangement Barcode uses, because a test that has to link libcore is a worse
# test than one that lives next to the code it covers.

cmake_minimum_required(VERSION 3.21)
project(spin-host-tests CXX C)

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
        "the tests take GoogleTest, the kernel test doubles and the test FIT "
        "reader from there."
    )
endif()
set(UNA_SDK_ROOT "$ENV{UNA_SDK}")

# Take the SDK's pinned GoogleTest revision rather than choosing our own, so a
# failure here means the same thing it means in the SDK's own host tests.
include("${UNA_SDK_ROOT}/Tests/Host/cmake/FetchGoogleTest.cmake")

enable_testing()

# HrHold is plain C++ over no SDK types at all -- the one piece of the
# heart-rate path with a rule in it -- so it builds against nothing but
# GoogleTest.
add_executable(spin-hrhold-tests
    HrHold_test.cpp
    ZoneLadder_test.cpp
)
target_include_directories(spin-hrhold-tests PRIVATE "${APP_LIBS}/Header")
target_link_libraries(spin-hrhold-tests PRIVATE GTest::gtest_main)
add_test(NAME spin-hrhold-tests COMMAND spin-hrhold-tests)

add_executable(spin-activitywriter-tests
    ActivityWriter_test.cpp
    "${APP_LIBS}/Sources/ActivityWriter.cpp"
    "${UNA_SDK_ROOT}/Tests/Host/support/KernelTestDoubles.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/Fit/FitCrc.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/Fit/FitWriter.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/Fit/FitRecordCadence.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/Fit/RecordingMarker.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/JSON/JsonStreamWriter.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/UnaLogger/Logger.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/Wrappers/StdLibWrappers.c"
)

target_include_directories(spin-activitywriter-tests PRIVATE
    "${APP_LIBS}/Header"
    "${UNA_SDK_ROOT}/Libs/Header"
    "${UNA_SDK_ROOT}/Tests/Host"           # fit/FitReader.hpp
    "${UNA_SDK_ROOT}/Tests/Host/support"   # KernelTestDoubles.hpp
)

target_link_libraries(spin-activitywriter-tests PRIVATE GTest::gtest_main)
add_test(NAME spin-activitywriter-tests COMMAND spin-activitywriter-tests)
