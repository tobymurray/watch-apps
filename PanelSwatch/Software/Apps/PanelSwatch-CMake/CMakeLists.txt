cmake_minimum_required(VERSION 3.21)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build")

set(APP_NAME "PanelSwatch")
set(APP_USER_NAME "Swatch")
set(APP_TYPE "Utility")
set(DEV_ID "UNA")
# sha256("https://github.com/tobymurray/watch-apps#panel-swatch")[0:8]
set(APP_ID "0BBBA45BD29B6032")
set(APP_USE_ICONS On)
set(RESOURCES_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../Resources")
set(OUTPUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../Output")
set(LIBS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../Libs")
set(CUSTOMGUI_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../CustomGUI")

# cmake/una-app.cmake gates GUI-ELF merging on TOUCHGFX_PATH.
set(TOUCHGFX_PATH "${CUSTOMGUI_PATH}")

include($ENV{UNA_SDK}/cmake/una-app.cmake)
include($ENV{UNA_SDK}/cmake/una-sdk.cmake)
include(${LIBS_PATH}/libs.cmake)

project(${APP_NAME})
una_app_setup_version(BUILD_VERSION ${CMAKE_CURRENT_SOURCE_DIR})

set(SERVICE_SOURCES
    ${LIBS_SOURCES}
    ${UNA_SDK_SOURCES_COMMON}
    ${UNA_SDK_SOURCES_APPSYSTEM}
)
set(SERVICE_INCLUDE_DIRS
    ${LIBS_INCLUDE_DIRS}
    ${UNA_SDK_INCLUDE_DIRS_COMMON}
    ${UNA_SDK_INCLUDE_DIRS_SERVICE}
)
una_app_build_service(${APP_NAME}Service.elf)

set(GUI_SOURCES
    $ENV{UNA_SDK}/Libs/Source/AppSystem/EntryPoint/CustomGUI/main.cpp
    ${CUSTOMGUI_PATH}/Gui.cpp
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
