# Host tests for ButtonProbe.
#
# The measurement itself cannot be tested here -- it is a read of a hardware
# register on a watch. What can be, and is, is the reasoning applied to what
# comes back:
#
#   buttonprobe-pure-tests  -- pin selection and edge detection, over synthetic
#                              snapshots. No SDK, no registers.
#   buttonprobe-log-tests   -- the log file, over the SDK's in-memory
#                              filesystem. A copy of RunGuiProbe's, because the
#                              file it tests is a copy too.

cmake_minimum_required(VERSION 3.21)
project(buttonprobe-host-tests CXX C)

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

include("${UNA_SDK_ROOT}/Tests/Host/cmake/FetchGoogleTest.cmake")

enable_testing()

add_executable(buttonprobe-pure-tests
    PinWatch_test.cpp
    "${APP_LIBS}/Sources/PinWatch.cpp"
    "${APP_LIBS}/Sources/GpioPorts.cpp"
)
target_include_directories(buttonprobe-pure-tests PRIVATE "${APP_LIBS}/Header")
target_link_libraries(buttonprobe-pure-tests PRIVATE GTest::gtest_main)
add_test(NAME buttonprobe-pure-tests COMMAND buttonprobe-pure-tests)

add_executable(buttonprobe-log-tests
    ProbeLog_test.cpp
    "${APP_LIBS}/Sources/ProbeLog.cpp"
    "${UNA_SDK_ROOT}/Tests/Host/support/KernelTestDoubles.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/UnaLogger/Logger.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/Wrappers/StdLibWrappers.c"
)
target_include_directories(buttonprobe-log-tests PRIVATE
    "${APP_LIBS}/Header"
    "${UNA_SDK_ROOT}/Libs/Header"
    "${UNA_SDK_ROOT}/Tests/Host/support"
)
target_link_libraries(buttonprobe-log-tests PRIVATE GTest::gtest_main)
add_test(NAME buttonprobe-log-tests COMMAND buttonprobe-log-tests)
