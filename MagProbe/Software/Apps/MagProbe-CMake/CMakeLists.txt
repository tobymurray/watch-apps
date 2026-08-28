# MagProbe. Targets apps-v1.4.0 (KERNEL_INTERFACE_VERSION 3).
#
# An app carries the kernel interface version it was built against, and
# `Libs/Source/AppSystem/system.cpp` refuses to launch when the running kernel's
# version is lower than the app's. The check is one-directional and nothing
# catches it at build time: a .uapp built against a newer SDK than the watch's
# firmware packs identically and simply does not run. So $UNA_SDK has to point
# at apps-v1.4.0 or older.

cmake_minimum_required(VERSION 3.21)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build")

set(APP_NAME "MagProbe")
set(APP_USER_NAME "Mag Probe")
set(APP_TYPE "Utility")

# Off. This app answers a question when it is opened; it has nothing to record
# while nobody is looking, and an autostart app that claimed the accelerometer
# would contend with SleepLab, which has to run every night (SleepLab ledger
# row S8).
set(APP_AUTOSTART Off)

set(APP_USE_ICONS On)

set(DEV_ID "UNA")
# sha256("https://github.com/tobymurray/watch-apps#magprobe")[0:16], the repo
# convention. The anchor is the app's purpose, not its folder -- recompute it
# before changing either:
#
#   printf 'https://github.com/tobymurray/watch-apps#magprobe' \
#     | shasum -a 256 | cut -c1-16 | tr 'a-f' 'A-F'
set(APP_ID "506DB26A76D0DDDA")

set(RESOURCES_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../Resources")
set(OUTPUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../Output")
set(LIBS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../Libs")
set(CUSTOMGUI_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../CustomGUI")

# cmake/una-app.cmake gates GUI-ELF merging on TOUCHGFX_PATH, and a Utility app
# needs a GUI ELF whether or not it draws through TouchGFX (SleepLab ledger row
# P3: the merger requires one for every type except Glance). Pointing it at the
# CustomGUI directory is how RustGuiPoc satisfies that without linking TouchGFX.
set(TOUCHGFX_PATH "${CUSTOMGUI_PATH}")

include($ENV{UNA_SDK}/cmake/una-app.cmake)
include($ENV{UNA_SDK}/cmake/una-sdk.cmake)
include(${LIBS_PATH}/libs.cmake)

project(${APP_NAME})

una_app_setup_version(BUILD_VERSION ${CMAKE_CURRENT_SOURCE_DIR})
add_compile_definitions(MAGPROBE_VERSION="${BUILD_VERSION}")

# SENSOR for SDK::Sensor::Connection, which is what resolves a driver and is the
# whole first finding. Deliberately not UNA_SDK_SOURCES_SERVICE, which drags in
# FIT, TrackMap, Calibration and Variant: this app writes no FIT file, records no
# track and calibrates no stride.
set(SERVICE_SOURCES
    ${LIBS_SOURCES}
    ${UNA_SDK_SOURCES_COMMON}
    ${UNA_SDK_SOURCES_APPSYSTEM}
    ${UNA_SDK_SOURCES_SENSOR}
)
set(SERVICE_INCLUDE_DIRS
    ${LIBS_INCLUDE_DIRS}
    ${UNA_SDK_INCLUDE_DIRS_COMMON}
    ${UNA_SDK_INCLUDE_DIRS_SERVICE}
)
una_app_build_service(${APP_NAME}Service.elf)

# The GUI half links no TouchGFX: the CustomGUI entry point asks only for a
# class with a kernel constructor and a run().
set(GUI_SOURCES
    $ENV{UNA_SDK}/Libs/Source/AppSystem/EntryPoint/CustomGUI/main.cpp
    ${CUSTOMGUI_PATH}/Gui.cpp
    ${CUSTOMGUI_PATH}/Canvas.cpp
    ${CUSTOMGUI_PATH}/Render.cpp
    ${UNA_SDK_SOURCES_COMMON}
)
set(GUI_INCLUDE_DIRS
    ${LIBS_INCLUDE_DIRS}
    ${UNA_SDK_INCLUDE_DIRS_COMMON}
    ${CUSTOMGUI_PATH}
)
una_app_build_gui(${APP_NAME}GUI.elf)

una_app_build_app()
