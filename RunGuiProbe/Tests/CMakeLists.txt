# Host tests for RunGuiProbe, built out of tree like the app.
#
# Most of this app cannot be tested on a host, and saying so is part of the
# point: the question it asks is what a *kernel* does with a message, and no
# double can answer that. What is here is the two pieces whose failure would be
# invisible on the watch -- the dwell timer, and the log file that is the app's
# only output.
#
#   runguiprobe-pure-tests  -- the dwell and the wording. No SDK at all.
#   runguiprobe-log-tests   -- the log file, over the SDK's in-memory
#                              filesystem, including the handle-leak and
#                              write-failure cases.

cmake_minimum_required(VERSION 3.21)
project(runguiprobe-host-tests CXX C)

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

add_executable(runguiprobe-pure-tests
    ProbePlan_test.cpp
    "${APP_LIBS}/Sources/ProbePlan.cpp"
)
target_include_directories(runguiprobe-pure-tests PRIVATE "${APP_LIBS}/Header")
target_link_libraries(runguiprobe-pure-tests PRIVATE GTest::gtest_main)
add_test(NAME runguiprobe-pure-tests COMMAND runguiprobe-pure-tests)

add_executable(runguiprobe-log-tests
    ProbeLog_test.cpp
    "${APP_LIBS}/Sources/ProbeLog.cpp"
    "${UNA_SDK_ROOT}/Tests/Host/support/KernelTestDoubles.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/UnaLogger/Logger.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/Wrappers/StdLibWrappers.c"
)
target_include_directories(runguiprobe-log-tests PRIVATE
    "${APP_LIBS}/Header"
    "${UNA_SDK_ROOT}/Libs/Header"
    "${UNA_SDK_ROOT}/Tests/Host/support"
)
target_link_libraries(runguiprobe-log-tests PRIVATE GTest::gtest_main)
add_test(NAME runguiprobe-log-tests COMMAND runguiprobe-log-tests)
