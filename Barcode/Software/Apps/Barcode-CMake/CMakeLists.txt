# Watch App CMakeLists.txt - Simplified version using SDK
cmake_minimum_required(VERSION 3.21)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build")

# App configuration
set(APP_NAME "Barcode")
set(APP_USER_NAME "Barcode")
set(APP_TYPE "Utility")
set(DEV_ID "UNA")
# Identity of the app in the store, and how the mobile app matches a new
# *.uapp to the installed one when the file name changes. Stable for the life
# of the app: it must equal "id" in app-manifest.json.
set(APP_ID "409506B8B69EC13E")
set(RESOURCES_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../Resources")
set(OUTPUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../Output")
set(LIBS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../Libs")
set(CUSTOMGUI_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../CustomGUI")
set(RUST_CRATE_DIR "${CUSTOMGUI_PATH}/rust")
# cmake/una-app.cmake gates GUI-ELF merging on TOUCHGFX_PATH; there is no
# TouchGFX here any more, this only satisfies that gate.
set(TOUCHGFX_PATH "${CUSTOMGUI_PATH}")

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

project(${APP_NAME})

# Set up version
una_app_setup_version(BUILD_VERSION ${CMAKE_CURRENT_SOURCE_DIR})

set(RUST_TARGET "thumbv8m.main-none-eabihf")
set(RUST_LIB "${RUST_CRATE_DIR}/target/${RUST_TARGET}/release/libbarcode_gui.a")

find_program(CARGO_EXECUTABLE cargo REQUIRED)

# Always invoke cargo and let it decide whether to rebuild -- an OUTPUT/DEPENDS
# rule would have to restate cargo's own inputs and anything it missed would
# silently link a stale archive against a changed ABI. See RustGuiPoc/Docs/
# FINDINGS.md, "Let cargo decide when to rebuild".
add_custom_target(barcode_gui_rust ALL
    COMMAND ${CARGO_EXECUTABLE} build --release --target ${RUST_TARGET}
    WORKING_DIRECTORY ${RUST_CRATE_DIR}
    BYPRODUCTS ${RUST_LIB}
    COMMENT "Building Rust GUI core -> libbarcode_gui.a"
    VERBATIM
)

# Build service
# The app owns no sensors and has nothing to time, so the FIT, sensor,
# track-map and calibration groups of UNA_SDK_SOURCES_SERVICE are deliberately
# left out, same as Stopwatch. APPCONFIG is in: the service reads the user's
# id through SDK::AppConfig, and that needs JSON in the same link.
set(SERVICE_SOURCES
    ${LIBS_SOURCES}
    ${UNA_SDK_SOURCES_COMMON}
    ${UNA_SDK_SOURCES_APPSYSTEM}
    ${UNA_SDK_SOURCES_JSON}
    ${UNA_SDK_SOURCES_APPCONFIG}
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

# una_app_build_gui() links TOUCHGFX_LIBS into the GUI ELF.
set(TOUCHGFX_LIBS ${RUST_LIB})

una_app_build_gui(${APP_NAME}GUI.elf)
add_dependencies(${APP_NAME}GUI.elf barcode_gui_rust)

# Build the complete app using SDK functions
una_app_build_app()
