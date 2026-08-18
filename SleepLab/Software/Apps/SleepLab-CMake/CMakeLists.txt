# SleepLab. Targets apps-v1.4.0 (KERNEL_INTERFACE_VERSION 3).
#
# Confirmed 2026-08-18 that the watch runs the 1.4 firmware line, so there is
# no 1.3 fallback here: SDK::send_msg rather than a SendMsg.hpp backport,
# SDK/HomeWidget available, and no -fcyclomatic-complexity probe (opt-in since
# PR #232). An app built against 3 exits instantly to an `App PID` error screen
# on a v2 kernel and nothing catches that at build time -- see the table in
# ../../../README.md.

cmake_minimum_required(VERSION 3.21)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build")

set(APP_NAME "SleepLab")
set(APP_USER_NAME "Sleep Lab")
set(APP_TYPE "Utility")
# Autostart, because the recording has to happen whether or not anyone opens
# the app -- and because at 23:00 nobody is going to.
set(APP_AUTOSTART On)
set(DEV_ID "UNA")
# sha256("https://github.com/tobymurray/watch-apps#sleeplab")[0:8], the repo
# convention. The anchor is the app's purpose, not its folder -- recompute it
# before changing either.
set(APP_ID "E4782CD726259DC6")

set(RESOURCES_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../Resources")
set(OUTPUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../Output")
set(LIBS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../Libs")
set(TOUCHGFX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../TouchGFX-GUI")

include($ENV{UNA_SDK}/cmake/una-app.cmake)
include($ENV{UNA_SDK}/cmake/una-sdk.cmake)
include(${LIBS_PATH}/libs.cmake)

if(DEFINED TOUCHGFX_PATH)
    include(${TOUCHGFX_PATH}/touchgfx.cmake)
endif()

project(${APP_NAME})

una_app_setup_version(BUILD_VERSION ${CMAKE_CURRENT_SOURCE_DIR})

# APPSYSTEM for the service entry point, SENSOR for SensorConnection, JSON for
# the settings reader and the summary writer. Deliberately not
# UNA_SDK_SOURCES_SERVICE, which also drags in FIT, TrackMap, Calibration and
# Variant: this app writes no FIT file (SDK/Fit/FitProfile.hpp has no
# monitoring or sleep messages -- see the README), records no track, and
# calibrates no stride.
set(SERVICE_SOURCES
    ${LIBS_SOURCES}
    ${UNA_SDK_SOURCES_COMMON}
    ${UNA_SDK_SOURCES_APPSYSTEM}
    ${UNA_SDK_SOURCES_SENSOR}
    ${UNA_SDK_SOURCES_JSON}
)
set(SERVICE_INCLUDE_DIRS
    ${LIBS_INCLUDE_DIRS}
    ${UNA_SDK_INCLUDE_DIRS_COMMON}
    ${UNA_SDK_INCLUDE_DIRS_JSON}
)
una_app_build_service(${APP_NAME}Service.elf)

if(DEFINED TOUCHGFX_PATH)
    set(GUI_SOURCES
        ${TOUCHGFX_SOURCES}
        ${UNA_SDK_SOURCES_COMMON}
        ${UNA_SDK_SOURCES_GUI}
    )
    set(GUI_INCLUDE_DIRS
        ${LIBS_INCLUDE_DIRS}
        ${UNA_SDK_INCLUDE_DIRS_COMMON}
        ${UNA_SDK_INCLUDE_DIRS_GUI}
        ${TOUCHGFX_INCLUDE_DIRS}
    )
    una_app_build_gui(${APP_NAME}GUI.elf)
endif()

una_app_build_app()
