# Host tests for Slumber's SDK-free code: the card Slumber publishes and
# SlumberGlance reads, the sleep-period finder, and the recorder's rows. All of
# it is header-only, so this needs GoogleTest, the SDK's FakeFileSystem, and
# SleepLab's counter to compare against.

cmake_minimum_required(VERSION 3.21)
project(slumber-host-tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Wpedantic)

if("$ENV{UNA_SDK}" STREQUAL "")
    message(FATAL_ERROR "UNA_SDK environment variable is not set. Set UNA_SDK to the SDK root.")
endif()
set(UNA_SDK_ROOT "$ENV{UNA_SDK}")

include("${UNA_SDK_ROOT}/Tests/Host/cmake/FetchGoogleTest.cmake")

enable_testing()

add_executable(slumber-card-tests
    SlumberCard_test.cpp
    CardStore_test.cpp
)
target_include_directories(slumber-card-tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../Software/Libs/Header/Card"
    "${UNA_SDK_ROOT}/Libs/Header"
    "${UNA_SDK_ROOT}/Tests/Host/support"
)
target_link_libraries(slumber-card-tests PRIVATE GTest::gtest_main)
add_test(NAME slumber-card-tests COMMAND slumber-card-tests)

add_executable(slumber-sleep-tests
    SleepPeriods_test.cpp
    SleepPeriodsReplay_test.cpp
    LastNight_test.cpp
    NightSummary_test.cpp
    Rhythm_test.cpp
)
target_include_directories(slumber-sleep-tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../Software/Libs/Header/Sleep"
)
target_link_libraries(slumber-sleep-tests PRIVATE GTest::gtest_main)
add_test(NAME slumber-sleep-tests COMMAND slumber-sleep-tests)

# SleepLab's counter is compiled in so the recorder's counts can be held to it.
add_executable(slumber-recorder-tests
    Recorder_test.cpp
    "${CMAKE_CURRENT_LIST_DIR}/../../SleepLab/Software/Libs/Sources/Engine/EpochCounter.cpp"
)
target_include_directories(slumber-recorder-tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../Software/Libs/Header/Sleep"
    "${CMAKE_CURRENT_LIST_DIR}/../Software/Libs/Header/Recorder"
    "${CMAKE_CURRENT_LIST_DIR}/../../SleepLab/Software/Libs/Header"
    "${UNA_SDK_ROOT}/Libs/Header"
    "${UNA_SDK_ROOT}/Tests/Host/support"
)
target_link_libraries(slumber-recorder-tests PRIVATE GTest::gtest_main)
add_test(NAME slumber-recorder-tests COMMAND slumber-recorder-tests)
