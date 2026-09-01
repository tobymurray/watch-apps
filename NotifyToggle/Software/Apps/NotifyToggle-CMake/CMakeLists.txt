cmake_minimum_required(VERSION 3.21)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build")

set(APP_NAME "NotifyToggle")
set(APP_USER_NAME "NotifyToggle")
set(APP_TYPE "Utility")
set(DEV_ID "UNA")
# sha256("https://github.com/tobymurray/watch-apps#notify-toggle")[0:8]
set(APP_ID "F37173F1AD6F4187")
set(APP_USE_ICONS On)
set(RESOURCES_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../Resources")
set(OUTPUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../Output")
set(LIBS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../Libs")
set(CUSTOMGUI_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../CustomGUI")
set(RUST_CRATE_DIR "${CUSTOMGUI_PATH}/rust")

# cmake/una-app.cmake gates GUI-ELF merging on TOUCHGFX_PATH; there is no
# TouchGFX here, this only satisfies that gate.
set(TOUCHGFX_PATH "${CUSTOMGUI_PATH}")

if("$ENV{UNA_SDK}" STREQUAL "")
    message(FATAL_ERROR "UNA_SDK environment variable is not set. Set it to the SDK root.")
endif()

include($ENV{UNA_SDK}/cmake/una-app.cmake)
include($ENV{UNA_SDK}/cmake/una-sdk.cmake)
include(${LIBS_PATH}/libs.cmake)

project(${APP_NAME})
una_app_setup_version(BUILD_VERSION ${CMAKE_CURRENT_SOURCE_DIR})

set(RUST_TARGET "thumbv8m.main-none-eabihf")
set(RUST_LIB "${RUST_CRATE_DIR}/target/${RUST_TARGET}/release/libnotify_toggle_gui.a")

find_program(CARGO_EXECUTABLE cargo REQUIRED)

# Always invoke cargo and let it decide whether to rebuild -- see
# RustGuiPoc/Docs/FINDINGS.md ("Let cargo decide when to rebuild") for why an
# OUTPUT/DEPENDS rule is the wrong shape here: it would have to restate
# cargo's own inputs, and anything the list missed would silently link a
# stale archive against a changed ABI.
add_custom_target(notify_toggle_gui_rust ALL
    COMMAND ${CARGO_EXECUTABLE} build --release --target ${RUST_TARGET}
    WORKING_DIRECTORY ${RUST_CRATE_DIR}
    BYPRODUCTS ${RUST_LIB}
    COMMENT "Building Rust GUI core -> libnotify_toggle_gui.a"
    VERBATIM
)

# The toggle is read, drawn, flipped and persisted entirely from the GUI
# process (see Gui.cpp / SettingsFile.cpp), talking to the real settings.json
# directly through SDK::Kernel::fs -- there is no SDK::AppConfig or JSON
# dependency anywhere in this app any more (see the README for why).
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
    # Not all of LIBS_SOURCES: Service.cpp is a do-nothing stub (see its own
    # comment) and has no reason to be linked into the GUI binary too.
    ${LIBS_PATH}/Sources/SettingsFile.cpp
    ${LIBS_PATH}/Sources/SettingsPatch.cpp
    ${LIBS_PATH}/Sources/DebugLog.cpp
    ${LIBS_PATH}/Sources/LiveSettings.cpp
    ${LIBS_PATH}/Sources/SettingsPersist.cpp
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
add_dependencies(${APP_NAME}GUI.elf notify_toggle_gui_rust)

una_app_build_app()
