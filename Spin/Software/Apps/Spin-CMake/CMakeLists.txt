cmake_minimum_required(VERSION 3.21)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build")

# App configuration
set(APP_NAME "Spin")
set(APP_USER_NAME "Spin")
set(APP_TYPE "Activity")
set(DEV_ID "UNA")
# Assigned by UNA and stable for the life of the app: it is how the mobile app
# matches a new *.uapp to the installed one when the file name changes.
set(APP_ID "1B8D0B8FA049E33F")
set(RESOURCES_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../Resources")
set(OUTPUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../Output")
set(LIBS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../Libs")
set(CUSTOMGUI_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../CustomGUI")
set(RUST_CRATE_DIR "${CUSTOMGUI_PATH}/rust")
set(TRAINKIT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../../TrainKit")
# una-app.cmake gates GUI-ELF merging on TOUCHGFX_PATH; there is no TouchGFX
# here, so this only satisfies that gate.
set(TOUCHGFX_PATH "${CUSTOMGUI_PATH}")

if("$ENV{UNA_SDK}" STREQUAL "")
    message(FATAL_ERROR "UNA_SDK environment variable is not set. Run SDK/Utilities/Scripts/set-una-sdk-env.ps1 or set UNA_SDK to the SDK root.")
endif()

# Sets the toolchain, so it must come before project().
include($ENV{UNA_SDK}/cmake/una-app.cmake)
include($ENV{UNA_SDK}/cmake/una-sdk.cmake)
include(${LIBS_PATH}/libs.cmake)

project(${APP_NAME})

una_app_setup_version(BUILD_VERSION ${CMAKE_CURRENT_SOURCE_DIR})

set(RUST_TARGET "thumbv8m.main-none-eabihf")
set(RUST_LIB "${RUST_CRATE_DIR}/target/${RUST_TARGET}/release/libspin_gui.a")
set(TRAINKIT_LIB "${TRAINKIT_PATH}/target/${RUST_TARGET}/release/libtrainkit.a")

find_program(CARGO_EXECUTABLE cargo REQUIRED)

# Always invoke cargo and let it decide whether to rebuild: an OUTPUT/DEPENDS
# rule restating cargo's inputs would silently link a stale archive against a
# changed ABI. See RustGuiPoc/Docs/FINDINGS.md.
add_custom_target(spin_gui_rust ALL
    COMMAND ${CARGO_EXECUTABLE} build --release --target ${RUST_TARGET}
    WORKING_DIRECTORY ${RUST_CRATE_DIR}
    BYPRODUCTS ${RUST_LIB}
    COMMENT "Building Rust GUI core -> libspin_gui.a"
    VERBATIM
)

# The Service's Rust, and the first archive it has ever linked. Separate from
# the GUI's because they land in different ELFs: two archives each carrying a
# #[panic_handler] would collide if they landed in one.
add_custom_target(spin_trainkit_rust ALL
    COMMAND ${CARGO_EXECUTABLE} build --release --target ${RUST_TARGET}
    WORKING_DIRECTORY ${TRAINKIT_PATH}
    BYPRODUCTS ${TRAINKIT_LIB}
    COMMENT "Building TrainKit -> libtrainkit.a"
    VERBATIM
)

# Named groups rather than UNA_SDK_SOURCES_SERVICE, which is the whole set: a
# stationary ride has no map to draw and no compass to calibrate.
set(SERVICE_SOURCES
    ${LIBS_SOURCES}
    ${TRAINKIT_PATH}/cpp/EventLog.cpp
    ${TRAINKIT_PATH}/cpp/SharedLog.cpp
    ${UNA_SDK_SOURCES_COMMON}
    ${UNA_SDK_SOURCES_APPSYSTEM}
    ${UNA_SDK_SOURCES_FIT}
    ${UNA_SDK_SOURCES_JSON}
    ${UNA_SDK_SOURCES_APPCONFIG}
    ${UNA_SDK_SOURCES_SENSOR}
)
set(SERVICE_INCLUDE_DIRS
    ${LIBS_INCLUDE_DIRS}
    ${TRAINKIT_PATH}/include
    ${TRAINKIT_PATH}/cpp
    ${UNA_SDK_INCLUDE_DIRS_COMMON}
    ${UNA_SDK_INCLUDE_DIRS_SERVICE}
)
una_app_build_service(${APP_NAME}Service.elf)

target_link_libraries(${APP_NAME}Service.elf PRIVATE ${TRAINKIT_LIB})
add_dependencies(${APP_NAME}Service.elf spin_trainkit_rust)

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
add_dependencies(${APP_NAME}GUI.elf spin_gui_rust)

una_app_build_app()
