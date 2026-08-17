# Watch App CMakeLists.txt - Simplified version using SDK
cmake_minimum_required(VERSION 3.21)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build")

# App configuration
#
# Deliberately NOT autostart, unlike MapManager. This app reads 4 MB of flash
# and writes it to storage; that is something to be asked for, once, not
# something to begin on every boot. It also cannot make progress with the USB
# cable in, so an autostart dump would spend most of its life stopped.
#
# Built off MapManager's shell, which is itself a fork of Chrono, which is a fork
# of the SDK's Stopwatch example -- so the CMakeLists pattern, SendMsg.hpp and the
# TouchGFX scaffolding all trace back there. The dump logic is a port of the
# sweep-7 instrumentation named in the README.
set(APP_NAME "FwDump")

# Kept short because the *launcher* clips it, not because the header does.
#
# The .uapp header carries a char[16] name field, so "Firmware Dump" fits there
# comfortably -- but the kernel's app-list screen has far less room than that and
# clips both ends without an ellipsis: "Firmware Dump" rendered on the watch as
# "mware Dun", which looks like a corrupt app rather than a long name. About nine
# characters is the practical ceiling, which matches the stock apps ("Treadmill",
# "HRMonitor", "Bike Map" -- all nine or fewer).
#
# There is no separate short-name field to fall back on: app_merging.py takes one
# -name and the launcher shows that. So the display name has to be the short one.
set(APP_USER_NAME "FW Dump")

# NOT renamed along with it, deliberately. APP_NAME is the sandbox folder --
# Apps/FwDump/ -- which is where the chunk files and the manifest live. Renaming
# it would orphan the dump already on the device and break resume, for a folder
# name nobody sees.
set(APP_TYPE "Utility")
set(APP_AUTOSTART Off)
set(DEV_ID "UNA")
# sha256("https://github.com/tobymurray/watch-apps#firmwaredump")[0:8]. The
# anchor is the app's purpose rather than its folder name, which is why it reads
# "firmwaredump" and not "fwdump"; recompute it before changing either.
set(APP_ID "5D041A7EB1D16CAA")
set(RESOURCES_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../Resources")
set(OUTPUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../Output")
set(LIBS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../Libs")
set(TOUCHGFX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../TouchGFX-GUI")

if("$ENV{UNA_SDK}" STREQUAL "")
    message(FATAL_ERROR "UNA_SDK environment variable is not set. Run SDK/Utilities/Scripts/set-una-sdk-env.ps1 or set UNA_SDK to the SDK root.")
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

# Includes TOUCHGFX_SOURCES, TOUCHGFX_INCLUDE_DIRS
if(DEFINED TOUCHGFX_PATH)
    include(${TOUCHGFX_PATH}/touchgfx.cmake)
endif()

project(${APP_NAME})

# apps-v1.3.0 adds -fcyclomatic-complexity unconditionally in its own
# add_compile_options(). That flag is ST-CubeIDE-GCC-only; mainline
# arm-none-eabi-gcc rejects it outright, so a from-scratch checkout of that tag
# fails to build any app at all. Mainline SDK guards it behind a
# check_cxx_compiler_flag probe, but the fix cannot be backported into a tag
# that has already shipped, and this app targets 1.3 deliberately (see the
# README) because the watch runs the 1.3 kernel line.
#
# So probe the compiler the same way mainline does and drop the flag here when
# it is not supported. Directory-scope, applied after the include and before any
# target is created, so it reaches every target this app builds. On a toolchain
# that does support the flag nothing changes. Same guard MapManager carries.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-fcyclomatic-complexity UNA_APP_HAS_CYCLOMATIC_COMPLEXITY)
if(NOT UNA_APP_HAS_CYCLOMATIC_COMPLEXITY)
    get_directory_property(_una_app_opts COMPILE_OPTIONS)
    list(REMOVE_ITEM _una_app_opts -fcyclomatic-complexity)
    set_directory_properties(PROPERTIES COMPILE_OPTIONS "${_una_app_opts}")
    message(STATUS "Dropped -fcyclomatic-complexity: this compiler rejects it")
endif()

# Set up version
una_app_setup_version(BUILD_VERSION ${CMAKE_CURRENT_SOURCE_DIR})

# Build service
#
# The dump reads memory through a pointer and writes files through the plain
# SDK::Kernel filesystem interface, so it needs no extra source group: none of
# UNA_SDK_SOURCES_SERVICE's FIT, sensor, track-map or calibration groups apply
# to an app that owns no sensors and records no activity.
#
# UNA_SDK_SOURCES_JSON is here for DumpConfig's optional fwdump.json reader --
# it is its own group rather than part of COMMON, so an app that parses JSON has
# to ask for it (Barcode does the same for its input.json).
set(SERVICE_SOURCES
    ${LIBS_SOURCES}
    ${UNA_SDK_SOURCES_COMMON}
    ${UNA_SDK_SOURCES_APPSYSTEM}
    ${UNA_SDK_SOURCES_JSON}
)
set(SERVICE_INCLUDE_DIRS
    ${LIBS_INCLUDE_DIRS}
    ${UNA_SDK_INCLUDE_DIRS_COMMON}
    ${UNA_SDK_INCLUDE_DIRS_SERVICE}
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

# Build the complete app using SDK functions
una_app_build_app()
