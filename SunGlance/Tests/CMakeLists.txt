# Host tests for SunGlance, built out of tree like the app.
#
# Two executables, split on what they need rather than on what they cover:
#
#   sunglance-pure-tests    -- the solar core, the next-event policy, the
#                              coordinate parser and the wording. No SDK, no kernel, no
#                              filesystem. This is the whole feature's
#                              arithmetic, and keeping it buildable against
#                              nothing but GoogleTest is what keeps it arguable
#                              at a desk instead of at dawn.
#   sunglance-config-tests  -- reading input.json into a position, which goes
#                              through SDK::Kernel and so needs the SDK's
#                              kernel test doubles and its coreJSON-backed JSON
#                              reader.
#   sunglance-glance-tests  -- the real Service, driven by a scripted message
#                              queue, with the glance updates it sends
#                              captured. See GlanceHarness.hpp.
#
# The second target is the one that would have caught SleepLab's dead glance:
# every part of that app built and passed its own tests while the wiring
# between them sent nothing to the screen. A query path typed wrong in a config
# reader fails exactly the same way -- silently, and only on a watch.

cmake_minimum_required(VERSION 3.21)
project(sunglance-host-tests CXX C)

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

add_executable(sunglance-pure-tests
    Solar_test.cpp
    Schedule_test.cpp
    Fix_test.cpp
    Render_test.cpp
    "${APP_LIBS}/Sources/Solar.cpp"
    "${APP_LIBS}/Sources/Schedule.cpp"
    "${APP_LIBS}/Sources/Fix.cpp"
    "${APP_LIBS}/Sources/Render.cpp"
)
target_include_directories(sunglance-pure-tests PRIVATE "${APP_LIBS}/Header")
target_link_libraries(sunglance-pure-tests PRIVATE GTest::gtest_main)
add_test(NAME sunglance-pure-tests COMMAND sunglance-pure-tests)

# ---------------------------------------------------------------------------
# The config reader
#
# coreJSON is a submodule in the SDK checkout, so the target is skipped rather
# than failing configure when it has not been initialised -- the pure tests are
# still worth running on a machine that has not.
# ---------------------------------------------------------------------------

set(COREJSON_DIR "${UNA_SDK_ROOT}/ThirdParty/coreJSON/source")

if(EXISTS "${COREJSON_DIR}/core_json.c")
    add_executable(sunglance-glance-tests
        Glance_test.cpp
        "${APP_LIBS}/Sources/Service.cpp"
        "${APP_LIBS}/Sources/HomeConfig.cpp"
        "${APP_LIBS}/Sources/InputConfig.cpp"
        "${APP_LIBS}/Sources/Render.cpp"
        "${APP_LIBS}/Sources/Schedule.cpp"
        "${APP_LIBS}/Sources/Solar.cpp"
        "${APP_LIBS}/Sources/Fix.cpp"
        "${APP_LIBS}/Sources/WallClock.cpp"
        "${UNA_SDK_ROOT}/Libs/Source/JSON/JsonStreamReader.cpp"
        "${COREJSON_DIR}/core_json.c"
        "${UNA_SDK_ROOT}/Tests/Host/support/KernelTestDoubles.cpp"
        "${UNA_SDK_ROOT}/Libs/Source/UnaLogger/Logger.cpp"
        "${UNA_SDK_ROOT}/Libs/Source/Wrappers/StdLibWrappers.c"
    )
    target_include_directories(sunglance-glance-tests PRIVATE
        "${APP_LIBS}/Header"
        "${COREJSON_DIR}/include"
        "${UNA_SDK_ROOT}/Libs/Header"
        "${UNA_SDK_ROOT}/Tests/Host/support"
        "${CMAKE_CURRENT_LIST_DIR}"
    )
    target_link_libraries(sunglance-glance-tests PRIVATE GTest::gtest_main)
    add_test(NAME sunglance-glance-tests COMMAND sunglance-glance-tests)
    # Every assertion in that suite is a local clock reading, so the zone the
    # suite runs in is part of the test rather than a property of the machine.
    set_tests_properties(sunglance-glance-tests PROPERTIES ENVIRONMENT "TZ=UTC")

    add_executable(sunglance-config-tests
        HomeConfig_test.cpp
        "${APP_LIBS}/Sources/HomeConfig.cpp"
        "${APP_LIBS}/Sources/InputConfig.cpp"
        "${APP_LIBS}/Sources/Fix.cpp"
        "${APP_LIBS}/Sources/Solar.cpp"
        "${UNA_SDK_ROOT}/Libs/Source/JSON/JsonStreamReader.cpp"
        "${COREJSON_DIR}/core_json.c"
        "${UNA_SDK_ROOT}/Tests/Host/support/KernelTestDoubles.cpp"
        "${UNA_SDK_ROOT}/Libs/Source/UnaLogger/Logger.cpp"
        "${UNA_SDK_ROOT}/Libs/Source/Wrappers/StdLibWrappers.c"
    )
    target_include_directories(sunglance-config-tests PRIVATE
        "${APP_LIBS}/Header"
        "${COREJSON_DIR}/include"
        "${UNA_SDK_ROOT}/Libs/Header"
        "${UNA_SDK_ROOT}/Tests/Host/support"
    )
    target_link_libraries(sunglance-config-tests PRIVATE GTest::gtest_main)
    add_test(NAME sunglance-config-tests COMMAND sunglance-config-tests)
else()
    message(STATUS "SunGlance: no coreJSON submodule, skipping sunglance-config-tests")
endif()
