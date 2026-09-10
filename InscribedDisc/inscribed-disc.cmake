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
# `inscribed_disc_rust_gui` has to run before `una_app_build_gui`, because it
# is what sets TOUCHGFX_PATH and TOUCHGFX_LIBS; the depends call has to run
# after, because the ELF target does not exist until then.
#
# The two variables it sets are not guessable:
#
#   TOUCHGFX_PATH  -- cmake/una-app.cmake gates GUI-ELF merging on this being
#                     set. There is no TouchGFX in a Rust GUI; it only satisfies
#                     that gate, and the value is the CustomGUI directory.
#   TOUCHGFX_LIBS  -- what una_app_build_gui() links into the GUI ELF.

if(NOT DEFINED CUSTOMGUI_PATH)
    message(FATAL_ERROR "inscribed-disc.cmake: set CUSTOMGUI_PATH before including it")
endif()

set(INSCRIBED_DISC_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}/Header)

# The target this SDK builds for. Named here so an app cannot drift from it.
set(INSCRIBED_DISC_RUST_TARGET "thumbv8m.main-none-eabihf")

# The feature an adopting crate turns the kit's panic handler on with.
if(NOT DEFINED INSCRIBED_DISC_CARGO_FEATURES)
    set(INSCRIBED_DISC_CARGO_FEATURES "device")
endif()

# Declares the cargo build for an app's renderer crate.
#
# `crate_name` is the crate's [package] name, whose archive cargo writes as
# lib<crate_name>.a. Sets INSCRIBED_DISC_RUST_LIB and
# INSCRIBED_DISC_RUST_TARGET_NAME.
function(inscribed_disc_rust_gui crate_name)
    set(_crate_dir "${CUSTOMGUI_PATH}/rust")
    set(_lib "${_crate_dir}/target/${INSCRIBED_DISC_RUST_TARGET}/release/lib${crate_name}.a")

    find_program(CARGO_EXECUTABLE cargo REQUIRED)

    # ALL, with no OUTPUT/DEPENDS pair: a rule with OUTPUT and no DEPENDS
    # rebuilds the archive only when it is missing, which links a stale renderer
    # against a changed ABI. Restating cargo's inputs in a DEPENDS list fails
    # the same way for anything the list misses, so let cargo decide.
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

    # Both of these have to be set before una_app_build_gui() runs. See the
    # header of this file for what each is for.
    set(TOUCHGFX_PATH "${CUSTOMGUI_PATH}" PARENT_SCOPE)
    set(TOUCHGFX_LIBS "${_lib}" PARENT_SCOPE)
endfunction()

# Orders the GUI ELF behind cargo. Call after una_app_build_gui().
function(inscribed_disc_gui_depends_on_rust gui_elf)
    if(NOT DEFINED INSCRIBED_DISC_RUST_TARGET_NAME)
        message(FATAL_ERROR "inscribed_disc_gui_depends_on_rust: call inscribed_disc_rust_gui() first")
    endif()
    add_dependencies(${gui_elf} ${INSCRIBED_DISC_RUST_TARGET_NAME})
endfunction()
