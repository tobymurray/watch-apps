cmake_minimum_required(VERSION 3.21)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build")

# A Glance and not a Utility, because the kernel gives a carousel card only to
# type 2 apps (Slumber/README.md). No autostart and no GUI: the carousel runs
# the service while the card is in view, and the card only reads what Slumber
# wrote.
set(APP_NAME "SlumberGlance")
# Not "Slumber": app_sorting.json keys apps by display name.
set(APP_USER_NAME "Slumber Glance")
set(APP_TYPE "Glance")
set(APP_AUTOSTART Off)
set(DEV_ID "UNA")
# Assigned by UNA and stable for the life of the app: it is how the mobile app
# matches a new *.uapp to the installed one, and it must equal "id" in app-manifest.json.
set(APP_ID "B97BB18D7A29150E")
set(APP_USE_ICONS Off)
set(OUTPUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../Output")
set(LIBS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../Libs")

if("$ENV{UNA_SDK}" STREQUAL "")
    message(FATAL_ERROR "UNA_SDK environment variable is not set. Set it to the SDK root.")
endif()

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

una_app_build_app()
