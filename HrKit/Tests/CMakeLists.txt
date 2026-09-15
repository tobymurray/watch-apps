# Host tests for HrKit. Plain C++ over no SDK types at all, so this builds
# against nothing but GoogleTest -- which is the property the kit exists to
# keep, and the reason both rules live here rather than in a Service.

cmake_minimum_required(VERSION 3.21)
project(hrkit-host-tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

if("$ENV{UNA_SDK}" STREQUAL "")
    message(FATAL_ERROR
        "UNA_SDK environment variable is not set. Set UNA_SDK to the SDK root; "
        "the tests take GoogleTest from there so a failure here means what it "
        "means there."
    )
endif()

include("$ENV{UNA_SDK}/Tests/Host/cmake/FetchGoogleTest.cmake")

enable_testing()

add_executable(hrkit-tests HrGate_test.cpp)
target_include_directories(hrkit-tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../Header")
target_link_libraries(hrkit-tests PRIVATE GTest::gtest_main)
add_test(NAME hrkit-tests COMMAND hrkit-tests)
