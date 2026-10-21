# InscribedDisc -- the build glue for an app whose GUI is a Rust renderer
# behind the SDK's CustomGUI entry point, included by that app's CMakeLists.txt
# with
#
#     set(CUSTOMGUI_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../CustomGUI")
#     set(INSCRIBED_DISC_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../../InscribedDisc")
#     include(${INSCRIBED_DISC_PATH}/inscribed-disc.cmake)
#     inscribed_disc_rust_gui(my_app_gui)  # the crate's [package] name
#     ...                                  # set GUI_SOURCES / GUI_INCLUDE_DIRS
#     una_app_build_gui(${APP_NAME}GUI.elf)
#     inscribed_disc_gui_depends_on_rust(${APP_NAME}GUI.elf)
#
# The order matters: `una_app_build_gui` reads the variables
# `inscribed_disc_rust_gui` sets, and creates the ELF target the depends call
# needs.

if(NOT DEFINED CUSTOMGUI_PATH)
    message(FATAL_ERROR "inscribed-disc.cmake: set CUSTOMGUI_PATH before including it")
endif()

set(INSCRIBED_DISC_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}/Header)

set(INSCRIBED_DISC_RUST_TARGET "thumbv8m.main-none-eabihf")

if(NOT DEFINED INSCRIBED_DISC_CARGO_FEATURES)
    set(INSCRIBED_DISC_CARGO_FEATURES "device")
endif()

# `crate_name` is the renderer crate's [package] name, built as
# lib<crate_name>.a.
function(inscribed_disc_rust_gui crate_name)
    set(_crate_dir "${CUSTOMGUI_PATH}/rust")
    set(_lib "${_crate_dir}/target/${INSCRIBED_DISC_RUST_TARGET}/release/lib${crate_name}.a")

    find_program(CARGO_EXECUTABLE cargo REQUIRED)

    # ALL with no OUTPUT, so cargo decides staleness: an OUTPUT rule rebuilt
    # only a missing archive and linked a stale renderer against a changed ABI.
    add_custom_target(${crate_name}_rust ALL
        COMMAND ${CARGO_EXECUTABLE} build --release
                --target ${INSCRIBED_DISC_RUST_TARGET}
                --features ${INSCRIBED_DISC_CARGO_FEATURES}
        WORKING_DIRECTORY ${_crate_dir}
        BYPRODUCTS ${_lib}
        COMMENT "Building Rust GUI core -> lib${crate_name}.a"
        VERBATIM
    )

    set(INSCRIBED_DISC_RUST_LIB "${_lib}" PARENT_SCOPE)
    set(INSCRIBED_DISC_RUST_TARGET_NAME "${crate_name}_rust" PARENT_SCOPE)

    # There is no TouchGFX here: TOUCHGFX_PATH only satisfies the GUI-ELF gate
    # in cmake/una-app.cmake, and TOUCHGFX_LIBS is what una_app_build_gui links.
    set(TOUCHGFX_PATH "${CUSTOMGUI_PATH}" PARENT_SCOPE)
    set(TOUCHGFX_LIBS "${_lib}" PARENT_SCOPE)
endfunction()

# Call after una_app_build_gui().
function(inscribed_disc_gui_depends_on_rust gui_elf)
    if(NOT DEFINED INSCRIBED_DISC_RUST_TARGET_NAME)
        message(FATAL_ERROR "inscribed_disc_gui_depends_on_rust: call inscribed_disc_rust_gui() first")
    endif()
    add_dependencies(${gui_elf} ${INSCRIBED_DISC_RUST_TARGET_NAME})
endfunction()
