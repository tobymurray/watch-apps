# Host tests for what the card says. CardView is header-only and reads the
# card through Slumber's own codec, so this needs GoogleTest and the SDK's
# headers and nothing else.

cmake_minimum_required(VERSION 3.21)
project(slumberglance-host-tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Wpedantic)

if("$ENV{UNA_SDK}" STREQUAL "")
    message(FATAL_ERROR "UNA_SDK environment variable is not set. Set UNA_SDK to the SDK root.")
endif()
set(UNA_SDK_ROOT "$ENV{UNA_SDK}")

include("${UNA_SDK_ROOT}/Tests/Host/cmake/FetchGoogleTest.cmake")

enable_testing()

add_executable(slumberglance-view-tests CardView_test.cpp)
target_include_directories(slumberglance-view-tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../Software/Libs/Header"
    "${CMAKE_CURRENT_LIST_DIR}/../../Slumber/Software/Libs/Header/Card"
    "${UNA_SDK_ROOT}/Libs/Header"
)
target_link_libraries(slumberglance-view-tests PRIVATE GTest::gtest_main)
add_test(NAME slumberglance-view-tests COMMAND slumberglance-view-tests)
