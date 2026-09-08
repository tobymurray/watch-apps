# Watch App CMakeLists.txt - built out of tree against $UNA_SDK
cmake_minimum_required(VERSION 3.21)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build")

# App configuration
set(APP_NAME "HapticLab")
set(APP_USER_NAME "Haptic Lab")
set(APP_TYPE "Utility")
set(DEV_ID "UNA")
# Identity of the app in the store, and how the mobile app matches a new
# *.uapp to the installed one when the file name changes. Stable for the life
# of the app: it must equal "id" in app-manifest.json.
set(APP_ID "5983D82DFEFF2626")     # sha256("https://github.com/tobymurray/watch-apps#hapticlab")[0:8]
set(RESOURCES_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../Resources")
set(OUTPUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../Output")
set(LIBS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../Libs")
set(CUSTOMGUI_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../CustomGUI")
set(RUST_CRATE_DIR "${CUSTOMGUI_PATH}/rust")
# cmake/una-app.cmake gates GUI-ELF merging on TOUCHGFX_PATH; there is no
# TouchGFX here, this only satisfies that gate.
set(TOUCHGFX_PATH "${CUSTOMGUI_PATH}")

if("$ENV{UNA_SDK}" STREQUAL "")
    message(FATAL_ERROR "UNA_SDK environment variable is not set. Set UNA_SDK to an apps-v1.4.0 SDK root.")
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
# Both halves stamp it into the run they write, so a run says which build
# produced it without anybody remembering.
add_compile_definitions(HAPTICLAB_VERSION="${BUILD_VERSION}")

set(RUST_TARGET "thumbv8m.main-none-eabihf")
set(RUST_LIB "${RUST_CRATE_DIR}/target/${RUST_TARGET}/release/libhaptic_lab_gui.a")

find_program(CARGO_EXECUTABLE cargo REQUIRED)

# Always invoke cargo and let it decide whether to rebuild: an OUTPUT/DEPENDS
# rule would have to restate cargo's own inputs and anything it missed would
# silently link a stale archive against a changed ABI. See RustGuiPoc/Docs/
# FINDINGS.md, "Let cargo decide when to rebuild".
add_custom_target(haptic_lab_gui_rust ALL
    COMMAND ${CARGO_EXECUTABLE} build --release --target ${RUST_TARGET}
    WORKING_DIRECTORY ${RUST_CRATE_DIR}
    BYPRODUCTS ${RUST_LIB}
    COMMENT "Building Rust GUI core -> libhaptic_lab_gui.a"
    VERBATIM
)

# Build service
#
# The FIT, track-map and calibration groups are deliberately absent: this app
# records no activity. SENSOR is in because Phase 1 subscribes to FUSION_RAW.
# JSON is not: only the GUI half reads config.json.
set(SERVICE_SOURCES
    ${LIBS_SOURCES_SERVICE}
    ${UNA_SDK_SOURCES_COMMON}
    ${UNA_SDK_SOURCES_APPSYSTEM}
    ${UNA_SDK_SOURCES_SENSOR}
)
set(SERVICE_INCLUDE_DIRS
    ${LIBS_INCLUDE_DIRS}
    ${UNA_SDK_INCLUDE_DIRS_COMMON}
    ${UNA_SDK_INCLUDE_DIRS_SERVICE}
    ${CUSTOMGUI_PATH}
)
una_app_build_service(${APP_NAME}Service.elf)

# The GUI half owns the protocol, so it links the app's own sources too --
# unlike most apps here, where Libs/ is the Service's.
set(GUI_SOURCES
    $ENV{UNA_SDK}/Libs/Source/AppSystem/EntryPoint/CustomGUI/main.cpp
    ${CUSTOMGUI_PATH}/Gui.cpp
    ${LIBS_SOURCES_GUI}
    ${UNA_SDK_SOURCES_COMMON}
    ${UNA_SDK_SOURCES_JSON}
)
set(GUI_INCLUDE_DIRS
    ${LIBS_INCLUDE_DIRS}
    ${UNA_SDK_INCLUDE_DIRS_COMMON}
    ${UNA_SDK_INCLUDE_DIRS_JSON}
    ${CUSTOMGUI_PATH}
)

# una_app_build_gui() links TOUCHGFX_LIBS into the GUI ELF.
set(TOUCHGFX_LIBS ${RUST_LIB})

una_app_build_gui(${APP_NAME}GUI.elf)
add_dependencies(${APP_NAME}GUI.elf haptic_lab_gui_rust)

# Build the complete app using SDK functions
una_app_build_app()
