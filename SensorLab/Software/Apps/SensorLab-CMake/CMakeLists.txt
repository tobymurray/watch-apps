# SensorLab. Targets apps-v1.4.0 (KERNEL_INTERFACE_VERSION 3).
#
# There is no 1.3 fallback here: SDK::send_msg rather than a SendMsg.hpp
# backport, and no -fcyclomatic-complexity probe (opt-in since PR #232). An app
# built against interface version 3 exits instantly to an `App PID` error screen
# on a v2 kernel and nothing catches that at build time -- see the table in
# ../../../README.md.
#
# **The firmware version is not context for this app, it is the primary key**:
# every profile is named with it and every comparison keys on it. SensorLab reads
# it from the kernel via `SDK::Message::RequestSystemInfo` rather than trusting
# this file or a human, which is what makes SleepLab's ledger row P1 answerable
# at all.

cmake_minimum_required(VERSION 3.21)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build")

set(APP_NAME "SensorLab")
set(APP_USER_NAME "Sensor Lab")
set(APP_TYPE "Utility")

# **Off, and this is a design decision rather than an omission.**
#
# Two autostart apps that both claim the accelerometer contend (SleepLab's ledger
# row S8); SleepLab is the one that has to run every night; and a profiler that
# silently competed with it would corrupt the thing it exists to measure. This
# app runs when it is opened, and a long unattended soak is started deliberately
# by the user.
#
# Note that a `Utility` app cannot be service-only -- the merger requires a GUI
# ELF for every type except `Glance` (row P3) -- and that every app is built
# glance-capable regardless of type (row P4), so a glance is available later
# without changing type and without costing autostart.
set(APP_AUTOSTART Off)

set(DEV_ID "UNA")
# sha256("https://github.com/tobymurray/watch-apps#sensorlab")[0:8], the repo
# convention. The anchor is the app's purpose, not its folder -- recompute it
# before changing either:
#
#   printf 'https://github.com/tobymurray/watch-apps#sensorlab' \
#     | shasum -a 256 | cut -c1-16 | tr 'a-f' 'A-F'
set(APP_ID "0D110CA4A2ADD105")

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

# The app's own version reaches the manifest through this, so a packed .uapp and
# the profile it produces cannot disagree about which build measured what.
add_compile_definitions(SENSORLAB_VERSION="${BUILD_VERSION}")

# APPSYSTEM for the service entry point, JSON for the settings reader and the
# profile writer. **Deliberately not UNA_SDK_SOURCES_SENSOR**: this app talks to
# the sensor layer through raw messages in `Probes/SensorBus.cpp` rather than
# through `SDK::Sensor::Connection`, because it needs full-width handles,
# `RequestList` and `RequestGetDesc` -- see the header for the three reasons.
# Also not UNA_SDK_SOURCES_SERVICE, which drags in FIT, TrackMap, Calibration and
# Variant: this app writes no FIT file, records no track and calibrates no
# stride.
set(SERVICE_SOURCES
    ${LIBS_SOURCES}
    ${UNA_SDK_SOURCES_COMMON}
    ${UNA_SDK_SOURCES_APPSYSTEM}
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
