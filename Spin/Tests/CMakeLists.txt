# Host tests for what Spin writes. The FIT reader they decode with lives under
# the SDK's Tests/, not its Libs/, so this needs an SDK *checkout*.
#
# The renderer's tests are not here: they are Rust unit tests inside the crate,
# next to the code they cover (`cargo test --features std`).

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

# The SDK's pinned GoogleTest, so a failure here means what it means there.
include("${UNA_SDK_ROOT}/Tests/Host/cmake/FetchGoogleTest.cmake")

enable_testing()

# Plain C++ over no SDK types at all, so these build against nothing but
# GoogleTest.
add_executable(spin-hrhold-tests
    HrHold_test.cpp
    ZoneLadder_test.cpp
    SecondsAccrual_test.cpp
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

# The recovery detector and the load arithmetic are Rust in the shared TrainKit
# crate, so `ctest` shells out to cargo rather than leaving them uncovered here.
# Only when cargo is on the host: a machine without it should still be able to
# run the two suites above.
find_program(CARGO_EXECUTABLE cargo)
if(CARGO_EXECUTABLE)
    add_test(NAME trainkit-tests
        COMMAND ${CARGO_EXECUTABLE} test --features std
    )
    set_tests_properties(trainkit-tests PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../../TrainKit"
    )
else()
    message(WARNING
        "cargo not found: TrainKit's tests -- the recovery gates and the shared "
        "log's bounds -- will not run here. CI runs them separately."
    )
endif()
