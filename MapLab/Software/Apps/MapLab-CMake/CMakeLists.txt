# MapLab -- the hardware measurements the vector-map pivot rests on.
#
# Targets apps-v1.4.0 (KERNEL_INTERFACE_VERSION 3), like SleepLab and its
# probe, and unlike the three map apps and Map Manager, which are pinned to
# apps-v1.3.0. That split is deliberate and it is the reason this app exists in
# this shape: the watch was confirmed running the 1.4 firmware line on
# 2026-08-18, and a measurement taken on a 1.3 build would be a measurement of
# a kernel this device no longer runs.
#
# It also means MapLab is the first evidence about whether MapKit's code
# behaves the same on 1.4 -- the vendored rawtiles Container is compiled here
# against 1.4 headers and exercised on a real pack (benches I10/I11).
#
# Not autostart. This is a bench somebody runs while holding the watch, not a
# recorder; a lab app that started itself at boot would spend battery on
# nobody's behalf.

cmake_minimum_required(VERSION 3.21)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build")

set(APP_NAME "MapLab")
set(APP_USER_NAME "Map Lab")
set(APP_TYPE "Utility")
set(APP_AUTOSTART Off)
set(DEV_ID "UNA")
# sha256("https://github.com/tobymurray/watch-apps#maplab")[0:16], the repo
# convention -- recompute it before changing the anchor or the folder.
set(APP_ID "CA0462AE630B62DE")

set(RESOURCES_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../Resources")
set(OUTPUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../Output")
set(LIBS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../Libs")
set(TOUCHGFX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../TouchGFX-GUI")
# MapKit by relative path within the repository, the same arrangement the three
# map apps use. Only the vendored rawtiles reader is taken: benches I10/I11
# time the real reader on a real pack, and reimplementing it here would measure
# something no app runs.
set(MAPKIT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../../MapKit")

include($ENV{UNA_SDK}/cmake/una-app.cmake)
include($ENV{UNA_SDK}/cmake/una-sdk.cmake)
include(${TOUCHGFX_PATH}/touchgfx.cmake)

project(${APP_NAME})

una_app_setup_version(BUILD_VERSION ${CMAKE_CURRENT_SOURCE_DIR})

# The Service does nothing but stay alive while the screen is open, so it gets
# exactly one source file. Every benchmark lives in the GUI, where a renderer
# would live -- see Software/Libs/Header/Commands.hpp.
set(SERVICE_SOURCES
    ${LIBS_PATH}/Sources/Service.cpp
    ${UNA_SDK_SOURCES_COMMON}
    ${UNA_SDK_SOURCES_APPSYSTEM}
)
set(SERVICE_INCLUDE_DIRS
    ${LIBS_PATH}/Header
    ${UNA_SDK_INCLUDE_DIRS_COMMON}
)
una_app_build_service(${APP_NAME}Service.elf)

# Listed explicitly rather than globbed. The pure core is four files that a
# future MapKit renderer inherits, and a stray file appearing in the build by
# accident is exactly what MapKit's own mapkit.cmake refuses to allow.
set(MAPLAB_SOURCES
    ${LIBS_PATH}/Sources/Palette.cpp
    ${LIBS_PATH}/Sources/Canvas.cpp
    ${LIBS_PATH}/Sources/VecScene.cpp
    ${LIBS_PATH}/Sources/SceneRender.cpp
    ${LIBS_PATH}/Sources/Cards.cpp
    ${LIBS_PATH}/Sources/BenchLog.cpp
    ${LIBS_PATH}/Sources/BenchSuite.cpp
    ${MAPKIT_PATH}/Sources/Container.cpp
)

set(GUI_SOURCES
    ${TOUCHGFX_SOURCES}
    ${MAPLAB_SOURCES}
    ${UNA_SDK_SOURCES_COMMON}
    ${UNA_SDK_SOURCES_GUI}
)
set(GUI_INCLUDE_DIRS
    ${LIBS_PATH}/Header
    ${MAPKIT_PATH}/Header
    ${UNA_SDK_INCLUDE_DIRS_COMMON}
    ${UNA_SDK_INCLUDE_DIRS_GUI}
    ${TOUCHGFX_INCLUDE_DIRS}
)
una_app_build_gui(${APP_NAME}GUI.elf)

# The log records which build produced each row. Without this every row in
# maplab_log.csv says "dev" and two runs a week apart are indistinguishable.
target_compile_definitions(${APP_NAME}GUI.elf PRIVATE BUILD_VERSION_STR="${BUILD_VERSION}")

una_app_build_app()
