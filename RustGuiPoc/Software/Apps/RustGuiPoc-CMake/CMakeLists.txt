# RustGuiPoc — a CustomGUI app whose GUI is rendered by Rust (embedded-graphics)
# instead of TouchGFX. Mirrors the Stopwatch app's CMake, swapping the TouchGFX
# GUI sources for the SDK's CustomGUI entry point + our Gui.cpp shim, and linking
# the Rust rendering staticlib.
cmake_minimum_required(VERSION 3.21)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build")

# App configuration
set(APP_NAME "RustGuiPoc")
set(APP_USER_NAME "Rust GUI")
set(APP_TYPE "Utility")
set(DEV_ID "UNA")
set(APP_ID "0DA61D462A61222A")           # sha256("https://github.com/tobymurray/watch-apps#rust-gui-poc")[0:8]
set(APP_USE_ICONS On)
set(RESOURCES_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../Resources")
set(OUTPUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../Output")
set(LIBS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../Libs")
set(CUSTOMGUI_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../CustomGUI")
set(RUST_CRATE_DIR "${CUSTOMGUI_PATH}/rust")

# PoC shortcut: the SDK's una_app_build_app() only merges the GUI ELF when
# TOUCHGFX_PATH is defined. We reuse that gate to get our CustomGUI ELF merged,
# WITHOUT including any touchgfx.cmake. A cleaner long-term fix is a dedicated
# CUSTOMGUI_PATH gate in cmake/una-app.cmake.
set(TOUCHGFX_PATH "${CUSTOMGUI_PATH}")

include($ENV{UNA_SDK}/cmake/una-app.cmake)   # sets toolchain (arm-none-eabi)
include($ENV{UNA_SDK}/cmake/una-sdk.cmake)   # UNA_SDK_SOURCES_*, include dirs
include(${LIBS_PATH}/libs.cmake)             # LIBS_SOURCES, LIBS_INCLUDE_DIRS

project(${APP_NAME})
una_app_setup_version(BUILD_VERSION ${CMAKE_CURRENT_SOURCE_DIR})

# ---------------------------------------------------------------------------
# Rust rendering core -> libpoc_gui.a (Cortex-M33 hard-float, no_std)
# ---------------------------------------------------------------------------
set(RUST_TARGET "thumbv8m.main-none-eabihf")
set(RUST_LIB "${RUST_CRATE_DIR}/target/${RUST_TARGET}/release/libpoc_gui.a")

find_program(CARGO_EXECUTABLE cargo REQUIRED)

add_custom_command(
    OUTPUT ${RUST_LIB}
    COMMAND ${CARGO_EXECUTABLE} build --release --target ${RUST_TARGET}
    WORKING_DIRECTORY ${RUST_CRATE_DIR}
    COMMENT "Building Rust GUI core (embedded-graphics) -> libpoc_gui.a"
    VERBATIM
)
add_custom_target(poc_gui_rust DEPENDS ${RUST_LIB})

# ---------------------------------------------------------------------------
# Service ELF — owns the accelerometer and forwards samples to the GUI.
# UNA_SDK_SOURCES_SENSOR is required: a GUI process cannot reach a sensor at all
# (SDK::Kernel is {sys, log, mem, comm, fs}), so the sensor lives here.
# ---------------------------------------------------------------------------
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

# ---------------------------------------------------------------------------
# GUI ELF (CustomGUI entry + our shim + Rust staticlib) — no TouchGFX
# ---------------------------------------------------------------------------
set(GUI_SOURCES
    ${CUSTOMGUI_PATH}/entrypoint/main.cpp   # vendored; see the file for why
    ${CUSTOMGUI_PATH}/Gui.cpp
    ${UNA_SDK_SOURCES_COMMON}
)
set(GUI_INCLUDE_DIRS
    ${LIBS_INCLUDE_DIRS}
    ${UNA_SDK_INCLUDE_DIRS_COMMON}
    ${CUSTOMGUI_PATH}                       # Gui.hpp + poc_gui.h on the include path
)

# PoC shortcut: una_app_build_gui() links `-l:libstdc++.a ${TOUCHGFX_LIBS}`, so
# TOUCHGFX_LIBS is the seam to pull in our Rust archive.
set(TOUCHGFX_LIBS ${RUST_LIB})

una_app_build_gui(${APP_NAME}GUI.elf)
add_dependencies(${APP_NAME}GUI.elf poc_gui_rust)

# ---------------------------------------------------------------------------
# Merge Service.elf + GUI.elf -> .uapp
# ---------------------------------------------------------------------------
una_app_build_app()
