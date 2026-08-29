# Host tests for BacklightPwm's engine, built out of tree like the app.
#
# The engine is the app: everything else is a screen and a message. It is
# testable because SoftPwm takes its pin and its clock as interfaces, so a fake
# clock advances deterministically and a fake pin records the waveform.
#
# What that catches is the failure that would ruin a run without failing: a duty
# cycle that is not the one requested. Six indistinguishable photographs prove
# nothing, and if the engine emitted the same waveform at every rung, nothing
# else in the app would notice.
#
# The plan tests are assertions about the experiment rather than the code, the
# load-bearing one being that the rungs still match the six brightness values
# BacklightProbe sent. The whole argument is a comparison between those two runs;
# if the numbers drift apart it evaporates silently.
#
# What is NOT testable here, and is not pretended to be:
#
#   - That GPIOF BSRR at 0x42021418 is the backlight. PwmPin is compiled out
#     entirely off ARM, so nothing here writes a register or could tell you the
#     address was wrong.
#   - That the cycle counter runs, or what the core clock is. Both are measured
#     on hardware at startup.
#   - What the kernel does when it and this app both write the pin. That is the
#     actual research question and it only exists on the watch.
#
# No test target for Service: it blocks on the kernel message queue and never
# returns, exactly as FwDump's does.

cmake_minimum_required(VERSION 3.21)
project(backlightpwm-host-tests CXX C)

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

add_executable(backlightpwm-tests
    SoftPwm_test.cpp
    PwmPlan_test.cpp
    DmaPwm_test.cpp
    "${APP_LIBS}/Sources/SoftPwm.cpp"
    "${APP_LIBS}/Sources/PwmPlan.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/UnaLogger/Logger.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/Wrappers/StdLibWrappers.c"
)

target_include_directories(backlightpwm-tests PRIVATE
    "${APP_LIBS}/Header"
    "${UNA_SDK_ROOT}/Libs/Header"
)

target_link_libraries(backlightpwm-tests PRIVATE GTest::gtest_main)
add_test(NAME backlightpwm-tests COMMAND backlightpwm-tests)
