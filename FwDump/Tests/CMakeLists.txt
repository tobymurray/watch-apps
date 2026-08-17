# Host tests for FwDump's dump core, built out of tree like the app.
#
# The point of these is narrow and important: the CRC has to agree, byte for
# byte, with the zlib.crc32 the host reassembler uses, and the chunk/manifest/
# resume logic has to be right before anything is run against real flash. Both
# are testable here because FlashDumper reads its region through a window
# pointer rather than a hardcoded address, so a std::vector stands in for flash.
#
# What is NOT testable here, and is not pretended to be: that 0x08000000 is
# readable from an app at all, and that a bare relative filename resolves into
# the app's own sandbox folder. Both are hardware-only claims -- see the README.
#
# Unlike MapManager's suite, nothing here needs the SDK's InMemoryDirectory: the
# resume scan probes the chunk filenames it already knows rather than
# enumerating a directory, so the kernel test doubles as shipped in
# apps-v1.3.0 are enough.

cmake_minimum_required(VERSION 3.21)
project(fwdump-host-tests CXX C)

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

add_executable(fwdump-dumper-tests
    Crc32_test.cpp
    DumpManifest_test.cpp
    DumpConfig_test.cpp
    FlashDumper_test.cpp
    "${APP_LIBS}/Sources/Crc32.cpp"
    "${APP_LIBS}/Sources/DumpManifest.cpp"
    "${APP_LIBS}/Sources/DumpConfig.cpp"
    "${APP_LIBS}/Sources/FlashDumper.cpp"
    "${UNA_SDK_ROOT}/Tests/Host/support/KernelTestDoubles.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/JSON/JsonStreamReader.cpp"
    "${UNA_SDK_ROOT}/ThirdParty/coreJSON/source/core_json.c"
    "${UNA_SDK_ROOT}/Libs/Source/UnaLogger/Logger.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/Wrappers/StdLibWrappers.c"
)

target_include_directories(fwdump-dumper-tests PRIVATE
    "${APP_LIBS}/Header"
    "${UNA_SDK_ROOT}/Libs/Header"
    "${UNA_SDK_ROOT}/Tests/Host/support"
    # DumpConfig goes through the SDK's JSON reader, which is a thin wrapper over
    # coreJSON -- so the tests need coreJSON's own header and translation unit,
    # not just the SDK's.
    "${UNA_SDK_ROOT}/ThirdParty/coreJSON/source/include"
)

target_link_libraries(fwdump-dumper-tests PRIVATE GTest::gtest_main)
add_test(NAME fwdump-dumper-tests COMMAND fwdump-dumper-tests)

# A second, deliberately separate target: it dumps a synthetic region to real
# files on disk so that the actual reassemble_dump.py can be run over the
# result. That is the only way to check the manifest contract against the
# program that consumes it, rather than against this repo's reading of its
# regexes. Not a gtest, because its output is a directory the host tooling then
# inspects -- see the README's "Verifying the contract" section.
add_executable(fwdump-export-synthetic
    export_synthetic.cpp
    "${APP_LIBS}/Sources/Crc32.cpp"
    "${APP_LIBS}/Sources/DumpManifest.cpp"
    "${APP_LIBS}/Sources/FlashDumper.cpp"
    "${UNA_SDK_ROOT}/Tests/Host/support/KernelTestDoubles.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/UnaLogger/Logger.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/Wrappers/StdLibWrappers.c"
)

target_include_directories(fwdump-export-synthetic PRIVATE
    "${APP_LIBS}/Header"
    "${UNA_SDK_ROOT}/Libs/Header"
    "${UNA_SDK_ROOT}/Tests/Host/support"
)
