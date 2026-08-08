# Host tests for MapManager's verification core, built out of tree like the app.
#
# PackCrcVerifier/PackTrustMarker write through SDK::Kernel, so these need the
# kernel test doubles and GoogleTest, both taken from the SDK checkout rather
# than duplicated here.

cmake_minimum_required(VERSION 3.21)
project(mapmanager-host-tests CXX C)

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

add_executable(mapmanager-verifier-tests
    PackCrcVerifier_test.cpp
    "${APP_LIBS}/Sources/PackCrcVerifier.cpp"
    "${UNA_SDK_ROOT}/Tests/Host/support/KernelTestDoubles.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/UnaLogger/Logger.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/Wrappers/StdLibWrappers.c"
)

target_include_directories(mapmanager-verifier-tests PRIVATE
    "${APP_LIBS}/Header"
    "${UNA_SDK_ROOT}/Libs/Header"
    "${UNA_SDK_ROOT}/Tests/Host/support"
)

target_link_libraries(mapmanager-verifier-tests PRIVATE GTest::gtest_main)
add_test(NAME mapmanager-verifier-tests COMMAND mapmanager-verifier-tests)
