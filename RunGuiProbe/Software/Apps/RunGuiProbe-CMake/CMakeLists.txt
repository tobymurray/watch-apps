# RunGuiProbe -- a Glance-type app that also carries a GUI, to find out whether
# the kernel will launch one on request.
#
# The interesting lines in this file are the two that every other glance app in
# this repo does not have: APP_TYPE stays "Glance" *and* a GUI ELF is built and
# merged. app_merging.py allows exactly this -- it makes the .gui optional for
# type Glance rather than forbidden ("absent, allowed for type Glance") -- and
# whether the kernel agrees is the question the app exists to answer.
cmake_minimum_required(VERSION 3.21)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build")

set(APP_NAME "RunGuiProbe")
set(APP_USER_NAME "RunGuiProbe")
set(APP_TYPE "Glance")
set(APP_AUTOSTART Off)
set(DEV_ID "UNA")
set(APP_ID "E54CA618B8BD8B02")   # sha256("https://github.com/tobymurray/watch-apps#rungui-probe")[0:8]

# No artwork: the watch draws a lettered tile for an app with no icons, which is
# all a probe needs. Whether a Glance-type app appears in the launcher at all is
# itself unknown -- if it does, launching it by hand is the control for the
# experiment, separating "the kernel will not load this GUI at all" from "the
# kernel will not load it *from a glance*".
set(APP_USE_ICONS Off)

set(OUTPUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../Output")
set(LIBS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../Libs")
set(CUSTOMGUI_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../CustomGUI")

# una_app_build_app() only merges the GUI ELF when TOUCHGFX_PATH is defined, so
# that gate is reused to get a CustomGUI ELF merged without including any of
# TouchGFX. Borrowed from RustGuiPoc, which says the same thing at more length;
# a dedicated CUSTOMGUI_PATH gate in cmake/una-app.cmake would be the clean fix.
set(TOUCHGFX_PATH "${CUSTOMGUI_PATH}")

if("$ENV{UNA_SDK}" STREQUAL "")
    message(FATAL_ERROR "UNA_SDK environment variable is not set. Set UNA_SDK to the SDK root.")
endif()

include($ENV{UNA_SDK}/cmake/una-app.cmake)   # sets the toolchain; must precede project()
include($ENV{UNA_SDK}/cmake/una-sdk.cmake)   # UNA_SDK_SOURCES_*, include dirs
include(${LIBS_PATH}/libs.cmake)             # LIBS_SOURCES, LIBS_INCLUDE_DIRS

project(${APP_NAME})

una_app_setup_version(BUILD_VERSION ${CMAKE_CURRENT_SOURCE_DIR})

# ---------------------------------------------------------------------------
# Service ELF -- the glance half
#
# UNA_SDK_SOURCES_APPSYSTEM rather than UNA_SDK_SOURCES_SERVICE: the latter also
# pulls in FIT, JSON, the sensor layer and the stride calibrators, none of which
# this app reads a byte of.
# ---------------------------------------------------------------------------
set(SERVICE_SOURCES
    ${LIBS_SOURCES}
    ${UNA_SDK_SOURCES_COMMON}
    ${UNA_SDK_SOURCES_APPSYSTEM}
)
set(SERVICE_INCLUDE_DIRS
    ${LIBS_INCLUDE_DIRS}
    ${UNA_SDK_INCLUDE_DIRS_COMMON}
)
una_app_build_service(${APP_NAME}Service.elf)

# ---------------------------------------------------------------------------
# GUI ELF -- the screen being asked for. CustomGUI entry point, no TouchGFX.
#
# The SDK's own entry point is used rather than a vendored copy: the include
# that made it unbuildable on Linux at apps-v1.3.0 (KernelProviderGui.hpp for a
# file named KernelProviderGUI.hpp) is fixed at apps-v1.4.0, which is the tag
# this app targets. RustGuiPoc still carries its copy for the older tag.
#
# Listed file by file rather than reusing LIBS_SOURCES, which would drag the
# service into the GUI binary.
# ---------------------------------------------------------------------------
set(GUI_SOURCES
    $ENV{UNA_SDK}/Libs/Source/AppSystem/EntryPoint/CustomGUI/main.cpp
    ${CUSTOMGUI_PATH}/Gui.cpp
    ${LIBS_PATH}/Sources/ProbeLog.cpp
    ${UNA_SDK_SOURCES_COMMON}
)
set(GUI_INCLUDE_DIRS
    ${LIBS_INCLUDE_DIRS}
    ${UNA_SDK_INCLUDE_DIRS_COMMON}
    ${CUSTOMGUI_PATH}                        # Gui.hpp, for the SDK's entry point
)

# una_app_build_gui() links `-l:libstdc++.a ${TOUCHGFX_LIBS}`. Empty here: there
# is no TouchGFX in this binary and nothing else to add.
set(TOUCHGFX_LIBS "")

una_app_build_gui(${APP_NAME}GUI.elf)

# ---------------------------------------------------------------------------
# Merge Service.elf + GUI.elf -> .uapp
# ---------------------------------------------------------------------------
una_app_build_app()
