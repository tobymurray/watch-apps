# Watch App CMakeLists.txt - Simplified version using SDK
cmake_minimum_required(VERSION 3.21)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build")

# App configuration
set(APP_NAME "RunMap")
set(APP_USER_NAME "Run Map")
set(APP_TYPE "Activity")
set(DEV_ID "UNA")
set(APP_ID "96CB8FD134CF855C")     # sha256("https://github.com/tobymurray/watch-apps#runmap")[0:8]
set(RESOURCES_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../Resources")
set(OUTPUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../Output")
set(LIBS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../Libs")
set(TOUCHGFX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../TouchGFX-GUI")
set(MAPKIT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../../MapKit")

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

# Includes MAPKIT_SOURCES, MAPKIT_INCLUDE_DIRS -- the map layer shared with the
# other map-enabled apps in this repository. Reached by relative path rather
# than copied: Kira checks out the whole repository and builds only `subdir`,
# and its one hard layout rule is that an app directory hold exactly one
# *-CMake project under Software/ -- which a sibling directory with no CMake
# project of its own cannot break. See MapKit/README.md.
include(${MAPKIT_PATH}/mapkit.cmake)

project(${APP_NAME})

# apps-v1.3.0 adds -fcyclomatic-complexity unconditionally in its own
# add_compile_options(). That flag is ST-CubeIDE-GCC-only; mainline
# arm-none-eabi-gcc rejects it outright, so a from-scratch checkout of that tag
# fails to build any app at all -- including the stock app this one is forked
# from. Mainline SDK guards it behind a check_cxx_compiler_flag probe, but that
# fix cannot be backported into a tag that has already shipped, and this app
# targets 1.3 deliberately (see the README) because 1.4 firmware is not out yet.
#
# So probe the compiler the same way mainline does and drop the flag here when
# it is not supported. Directory-scope, applied after the include and before any
# target is created, so it reaches every target this app builds. On a toolchain
# that does support the flag nothing changes. Same treatment as MapManager's
# CMakeLists in this repository.
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
set(SERVICE_SOURCES
    ${LIBS_SOURCES}
    ${UNA_SDK_SOURCES_COMMON}
    ${UNA_SDK_SOURCES_SERVICE}
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
        ${MAPKIT_SOURCES}
        ${UNA_SDK_SOURCES_COMMON}
        ${UNA_SDK_SOURCES_GUI}
    )
    set(GUI_INCLUDE_DIRS
        ${LIBS_INCLUDE_DIRS}
        ${MAPKIT_INCLUDE_DIRS}
        ${UNA_SDK_INCLUDE_DIRS_COMMON}
        ${UNA_SDK_INCLUDE_DIRS_GUI}
        ${TOUCHGFX_INCLUDE_DIRS}
    )
    una_app_build_gui(${APP_NAME}GUI.elf)
endif()

# Build the complete app using SDK functions
una_app_build_app()