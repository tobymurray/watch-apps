# Host tests for the Squash recorder path, built out of tree like the app.
#
# Split into two executables because they need very different things:
#
#   * ImuCsvRecorder is plain C++ over a caller-supplied sink — no SDK headers
#     at all — so its tests build against nothing but GoogleTest.
#   * ImuFileSink writes through SDK::Kernel, and its test asserts the round
#     trip by replaying what landed in storage through the simulator's
#     ImuFusionSource. That needs an SDK checkout, and specifically one
#     carrying the IMU fusion sensor source, which is not in the SDK mainline
#     yet (it lives on the SDK's `simulator-imu-source` branch). Rather than
#     fail the whole configure on a mainline SDK, that suite is skipped with a
#     message when the source is absent.

cmake_minimum_required(VERSION 3.21)
project(squash-host-tests CXX C)

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

# Take the SDK's pinned GoogleTest revision rather than choosing our own, so a
# failure here means the same thing it means in the SDK's own host tests.
include("${UNA_SDK_ROOT}/Tests/Host/cmake/FetchGoogleTest.cmake")

enable_testing()

add_executable(squash-recorder-tests
    ImuCsvRecorder_test.cpp
    "${APP_LIBS}/Sources/ImuCsvRecorder.cpp"
)

target_include_directories(squash-recorder-tests PRIVATE "${APP_LIBS}/Header")
target_link_libraries(squash-recorder-tests PRIVATE GTest::gtest_main)
add_test(NAME squash-recorder-tests COMMAND squash-recorder-tests)

set(IMU_FUSION_SOURCE
    "${UNA_SDK_ROOT}/Libs/Source/Simulator/Components/Sensors/Imu/ImuFusionSource.cpp"
)

if(EXISTS "${IMU_FUSION_SOURCE}")
    add_executable(squash-filesink-tests
        ImuFileSink_test.cpp
        "${APP_LIBS}/Sources/ImuCsvRecorder.cpp"
        "${APP_LIBS}/Sources/ImuFileSink.cpp"
        "${UNA_SDK_ROOT}/Tests/Host/support/KernelTestDoubles.cpp"
        "${UNA_SDK_ROOT}/Libs/Source/UnaLogger/Logger.cpp"
        "${UNA_SDK_ROOT}/Libs/Source/Wrappers/StdLibWrappers.c"
        "${IMU_FUSION_SOURCE}"
    )

    target_include_directories(squash-filesink-tests PRIVATE
        "${APP_LIBS}/Header"
        "${UNA_SDK_ROOT}/Libs/Header"
        "${UNA_SDK_ROOT}/Tests/Host/support"
    )

    target_link_libraries(squash-filesink-tests PRIVATE GTest::gtest_main)
    add_test(NAME squash-filesink-tests COMMAND squash-filesink-tests)
else()
    message(STATUS
        "Squash: skipping squash-filesink-tests — no IMU fusion sensor source at "
        "${IMU_FUSION_SOURCE}. Point UNA_SDK at an SDK checkout that carries it."
    )
endif()
