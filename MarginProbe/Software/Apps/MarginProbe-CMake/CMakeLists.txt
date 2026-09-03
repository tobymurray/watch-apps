# One source tree, eight apps. See Tools/build_all.sh.
#
# Everything that distinguishes one build from another is derived from a single
# -DPROBE_INSET, so the series cannot drift: the number on the screen, the folder
# on the watch, the AppID and the inset the rectangle is drawn at are all the
# same number, and there is no second place to forget to change.
cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED PROBE_INSET)
    message(FATAL_ERROR "PROBE_INSET is required, e.g. -DPROBE_INSET=4. Tools/build_all.sh passes it.")
endif()

# `box`   -- one rectangle at PROBE_INSET from every edge. The original series.
# `stair` -- a full-height bar in each of several columns from both edges, which
#            traces the clipping boundary instead of sampling it once per side.
# `ruler` -- six adjacent 1px columns from each edge, one colour per display
#            pixel, so the outermost surviving stripe names its own column and
#            the answer is read rather than measured.
# `proof` -- three filled staircases a pixel apart, stating the boundary model in
#            a form the panel can refute: a white ring exactly at it, dark grey
#            inside, mid grey one pixel outside that should not appear at all.
# `arc`   -- the band filled mid grey with black ticks and datum, so the glass
#            cut reads black, the untouched fill reads grey, and the carousel's
#            scroll arc reads white. Luminance, not hue: the backlight is blue
#            and collapses every hue toward it.
if(NOT DEFINED PROBE_STYLE)
    set(PROBE_STYLE "box")
endif()
set(PROBE_STYLES box stair ruler arc proof)
if(NOT PROBE_STYLE IN_LIST PROBE_STYLES)
    message(FATAL_ERROR "PROBE_STYLE must be one of ${PROBE_STYLES}, not '${PROBE_STYLE}'")
endif()

# App configuration
#
# Type `Glance`, no autostart, no GUI half -- the same shape as SunGlance, and
# for the same reasons. This app is only ever looked at, never opened.
# The staircase is one app, not a series, so it is named for what it is rather
# than for an inset it does not have.
if(PROBE_STYLE STREQUAL "proof")
    set(PROBE_SLUG "proof")
    set(APP_NAME "Proof")
elseif(PROBE_STYLE STREQUAL "arc")
    set(PROBE_SLUG "arc")
    set(APP_NAME "Arc")
elseif(PROBE_STYLE STREQUAL "ruler")
    set(PROBE_SLUG "ruler")
    set(APP_NAME "Ruler")
elseif(PROBE_STYLE STREQUAL "stair")
    set(PROBE_SLUG "stair")
    set(APP_NAME "Stair")
else()
    set(PROBE_SLUG "marginprobe${PROBE_INSET}")
    set(APP_NAME "Edge${PROBE_INSET}")
endif()
set(APP_USER_NAME "${APP_NAME}")
set(APP_TYPE "Glance")
set(APP_AUTOSTART Off)
set(DEV_ID "UNA")

# The repo convention: sha256(source URL + '#' + app) truncated to eight bytes.
# Computed rather than pasted, because eight hand-copied ids is eight chances to
# install two apps over each other -- the watch keys its app folders on this, so
# a collision is silent and looks like the build not having been copied.
string(SHA256 PROBE_ID_FULL "https://github.com/tobymurray/watch-apps#${PROBE_SLUG}")
string(SUBSTRING "${PROBE_ID_FULL}" 0 16 PROBE_ID)
string(TOUPPER "${PROBE_ID}" APP_ID)

# No artwork. Kira and the watch list both draw a lettered tile, which for a
# probe that will be deleted next week is the right amount of effort.
set(APP_USE_ICONS Off)
set(OUTPUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../Output")
set(LIBS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../Libs")

if("$ENV{UNA_SDK}" STREQUAL "")
    message(FATAL_ERROR "UNA_SDK environment variable is not set. Set UNA_SDK to the SDK root.")
endif()

include($ENV{UNA_SDK}/cmake/una-app.cmake)
include($ENV{UNA_SDK}/cmake/una-sdk.cmake)
include(${LIBS_PATH}/libs.cmake)

project(${APP_NAME})

una_app_setup_version(BUILD_VERSION ${CMAKE_CURRENT_SOURCE_DIR})

# Directory-scoped, so it reaches the service target the SDK creates below. This
# is the one thing the source file cannot get any other way.
add_compile_definitions(PROBE_INSET=${PROBE_INSET})
if(PROBE_STYLE STREQUAL "stair")
    add_compile_definitions(PROBE_STAIR=1)
elseif(PROBE_STYLE STREQUAL "ruler")
    add_compile_definitions(PROBE_RULER=1)
elseif(PROBE_STYLE STREQUAL "arc")
    add_compile_definitions(PROBE_ARC=1)
elseif(PROBE_STYLE STREQUAL "proof")
    add_compile_definitions(PROBE_PROOF=1)
endif()

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

una_app_build_app()
