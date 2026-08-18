# Tier 0 feasibility probe.
#
# It has a GUI only because it has to: `app_merging.py` makes the .gui ELF
# optional for type Glance and mandatory for everything else, so a Utility app
# cannot be service-only however little it draws. The screen that resulted
# earns its place anyway -- see gui/include/gui/main_screen/MainView.hpp.
#
# Targets apps-v1.4.0 (KERNEL_INTERFACE_VERSION 3). Confirmed 2026-08-18 that
# the watch runs the 1.4 firmware line, so there is no 1.3 fallback here: no
# SendMsg.hpp backport, and no -fcyclomatic-complexity probe (opt-in as of
# PR #232). See ../../../../README.md.

cmake_minimum_required(VERSION 3.21)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build")

set(APP_NAME "SleepProbe")
set(APP_USER_NAME "Sleep Probe")
set(APP_TYPE "Utility")
# Autostart, because the probe's whole job is to still be recording at 04:00
# and there is no screen to launch it from. Removing the app folder is how you
# stop it.
set(APP_AUTOSTART On)
set(DEV_ID "UNA")
# sha256("https://github.com/tobymurray/watch-apps#sleepprobe")[0:8], the repo
# convention. The anchor is the app's purpose, not its folder -- recompute it
# before changing either.
set(APP_ID "999A630E7F105A07")

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
# the config reader. Deliberately not UNA_SDK_SOURCES_SERVICE, which drags in
# FIT, TrackMap, Calibration and Variant -- none of which a counter uses, and
# all of which are flash this probe should not be carrying overnight.
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
