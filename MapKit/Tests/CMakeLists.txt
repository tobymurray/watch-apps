# Host tests for MapKit, built out of tree like the apps that use it.
#
# Two executables, because they need different things:
#
#   mapkit-pure-tests   -- MapMath, TraceBuffer and the pack-selection rule.
#                          No SDK, no kernel, no filesystem: these are the
#                          parts that are pure arithmetic and pure policy, and
#                          keeping them buildable on their own is what keeps
#                          them that way.
#   mapkit-kernel-tests -- PackTrustReader, PackCatalog and MapSession, which
#                          read files and therefore need the SDK's kernel test
#                          doubles.
#
# The kernel tests need the SDK's InMemoryDirectory -- the real enumerating
# fake, not the older EmptyDirectory stub that always reported no entries --
# because PackCatalog's whole job starts with a directory scan. That
# enhancement is not in apps-v1.3.0, so the app build and this test build want
# different SDK checkouts. Point UNA_SDK accordingly for each; ../README.md
# says which branch currently carries it.

cmake_minimum_required(VERSION 3.21)
project(mapkit-host-tests CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(MAPKIT "${CMAKE_CURRENT_LIST_DIR}/..")

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

add_executable(mapkit-pure-tests
    MapMath_test.cpp
    PackDepth_test.cpp
    PackSelection_test.cpp
    TraceBuffer_test.cpp
    "${MAPKIT}/Sources/PackDepth.cpp"
    "${MAPKIT}/Sources/PackSelection.cpp"
)
target_include_directories(mapkit-pure-tests PRIVATE "${MAPKIT}/Header")
target_link_libraries(mapkit-pure-tests PRIVATE GTest::gtest_main)
add_test(NAME mapkit-pure-tests COMMAND mapkit-pure-tests)

add_executable(mapkit-kernel-tests
    Container_test.cpp
    MapSession_test.cpp
    TileRequestLog_test.cpp
    PackCatalog_test.cpp
    PackTrustReader_test.cpp
    "${MAPKIT}/Sources/Container.cpp"
    "${MAPKIT}/Sources/MapSession.cpp"
    "${MAPKIT}/Sources/PackCatalog.cpp"
    "${MAPKIT}/Sources/PackDepth.cpp"
    "${MAPKIT}/Sources/PackSelection.cpp"
    "${MAPKIT}/Sources/TileRequestLog.cpp"
    "${UNA_SDK_ROOT}/Tests/Host/support/KernelTestDoubles.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/UnaLogger/Logger.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/Wrappers/StdLibWrappers.c"
)
target_include_directories(mapkit-kernel-tests PRIVATE
    "${MAPKIT}/Header"
    "${CMAKE_CURRENT_LIST_DIR}"
    "${UNA_SDK_ROOT}/Libs/Header"
    "${UNA_SDK_ROOT}/Tests/Host/support"
)
target_link_libraries(mapkit-kernel-tests PRIVATE GTest::gtest_main)
add_test(NAME mapkit-kernel-tests COMMAND mapkit-kernel-tests)
