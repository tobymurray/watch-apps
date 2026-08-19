# Watch App CMakeLists.txt - Simplified version using SDK
cmake_minimum_required(VERSION 3.21)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build")

# App configuration
#
# Type `Glance`, and no autostart. Sunrise is arithmetic over a date and a
# coordinate: there is nothing to accumulate, nothing to miss while the app is
# not running, and therefore nothing worth a process that lives from boot. The
# carousel starts this service, ticks it while the card is on screen, and the
# service returns from run() when the card scrolls away. See Service.hpp for
# what that choice costs -- a home widget, which this app type cannot push.
#
# No GUI half at all: there is no TouchGFX project here, so nothing is built
# for the screen and tapping the card opens nothing. Everything this app has to
# say fits in three text controls, which is the definition of a glance.
set(APP_NAME "SunGlance")
set(APP_USER_NAME "Sun")
set(APP_TYPE "Glance")
set(APP_AUTOSTART Off)
set(DEV_ID "UNA")
set(APP_ID "CCAC55621745C147")   # sha256("https://github.com/tobymurray/watch-apps#sunglance")[0:8]
# No artwork yet, so the packer is told not to look for any rather than being
# pointed at files that are not there. Kira draws a lettered tile for an app
# with no icons; the watch's own list does the same.
set(APP_USE_ICONS Off)
set(OUTPUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../Output")
set(LIBS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../Libs")

if("$ENV{UNA_SDK}" STREQUAL "")
    message(FATAL_ERROR "UNA_SDK environment variable is not set. Set UNA_SDK to the SDK root.")
endif()

# Includes all build steps and required tools
# Sets toolchain before project
include($ENV{UNA_SDK}/cmake/una-app.cmake)

# Includes sets: SCRIPTS_PATH,
# UNA_SDK_SOURCES_COMMON, UNA_SDK_SOURCES_SERVICE, UNA_SDK_SOURCES_GUI,
# UNA_SDK_INCLUDE_DIRS_COMMON, UNA_SDK_INCLUDE_DIRS_SERVICE, UNA_SDK_INCLUDE_DIRS_GUI
include($ENV{UNA_SDK}/cmake/una-sdk.cmake)

# Includes LIBS_SOURCES, LIBS_INCLUDE_DIRS
include(${LIBS_PATH}/libs.cmake)

project(${APP_NAME})

# Set up version
una_app_setup_version(BUILD_VERSION ${CMAKE_CURRENT_SOURCE_DIR})

# Build service
set(SERVICE_SOURCES
    ${LIBS_SOURCES}
    ${UNA_SDK_SOURCES_COMMON}
    ${UNA_SDK_SOURCES_SERVICE}
)
set(SERVICE_INCLUDE_DIRS
    ${LIBS_INCLUDE_DIRS}
    ${UNA_SDK_INCLUDE_DIRS_COMMON}
    ${UNA_SDK_INCLUDE_DIRS_SERVICE}
)
una_app_build_service(${APP_NAME}Service.elf)

# Build the complete app using SDK functions
una_app_build_app()
