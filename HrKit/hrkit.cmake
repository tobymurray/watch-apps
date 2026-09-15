# HrKit -- the two rules every app reading the heart-rate sensor needs, included
# by that app's CMakeLists.txt with
#
#     include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../HrKit/hrkit.cmake)
#
# and folded into that app's SERVICE_INCLUDE_DIRS (and GUI_INCLUDE_DIRS, if the
# GUI half decides what to draw).
#
# Header-only, so there is no source list to add to and nothing to link. That is
# deliberate: the moment this kit needs a .cpp it has grown something that holds
# state or touches the SDK, and neither belongs here.

set(HRKIT_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/Header
)
