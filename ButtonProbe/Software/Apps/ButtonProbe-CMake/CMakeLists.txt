# ButtonProbe -- find out which GPIO pin is which button, and whether a glance
# service can see one move.
#
# Same shape as RunGuiProbe, which established that a Glance-type app may carry
# a GUI and that the kernel will launch it: a glance half and a CustomGUI half,
# no TouchGFX. Here the GUI is not the thing being tested -- it is the labelled
# half of the measurement, because it is the only half the kernel tells which
# button was pressed.
cmake_minimum_required(VERSION 3.21)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build")

set(APP_NAME "ButtonProbe")
set(APP_USER_NAME "ButtonProbe")
set(APP_TYPE "Glance")
set(APP_AUTOSTART Off)
set(DEV_ID "UNA")
set(APP_ID "A6B4C5BC71F9CD3E")   # sha256("https://github.com/tobymurray/watch-apps#button-probe")[0:8]
set(APP_USE_ICONS Off)

set(OUTPUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../Output")
set(LIBS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../Libs")
set(CUSTOMGUI_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../CustomGUI")

# The GUI ELF is merged only when TOUCHGFX_PATH is defined; reused here to merge
# a CustomGUI binary without including any of TouchGFX. See RunGuiProbe.
set(TOUCHGFX_PATH "${CUSTOMGUI_PATH}")

if("$ENV{UNA_SDK}" STREQUAL "")
    message(FATAL_ERROR "UNA_SDK environment variable is not set. Set UNA_SDK to the SDK root.")
endif()

include($ENV{UNA_SDK}/cmake/una-app.cmake)
include($ENV{UNA_SDK}/cmake/una-sdk.cmake)
include(${LIBS_PATH}/libs.cmake)

project(${APP_NAME})

una_app_setup_version(BUILD_VERSION ${CMAKE_CURRENT_SOURCE_DIR})

# ---------------------------------------------------------------------------
# Service ELF -- the glance half
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
# GUI ELF -- the labelled half. CustomGUI entry point, no TouchGFX.
#
# Listed file by file rather than reusing LIBS_SOURCES, which would drag the
# glance service into the GUI binary.
# ---------------------------------------------------------------------------
set(GUI_SOURCES
    $ENV{UNA_SDK}/Libs/Source/AppSystem/EntryPoint/CustomGUI/main.cpp
    ${CUSTOMGUI_PATH}/Gui.cpp
    ${LIBS_PATH}/Sources/ProbeLog.cpp
    ${LIBS_PATH}/Sources/PinWatch.cpp
    ${LIBS_PATH}/Sources/GpioPorts.cpp
    ${LIBS_PATH}/Sources/Sampler.cpp
    ${UNA_SDK_SOURCES_COMMON}
)
set(GUI_INCLUDE_DIRS
    ${LIBS_INCLUDE_DIRS}
    ${UNA_SDK_INCLUDE_DIRS_COMMON}
    ${CUSTOMGUI_PATH}
)

set(TOUCHGFX_LIBS "")

una_app_build_gui(${APP_NAME}GUI.elf)

una_app_build_app()
