# MapKit -- the shared map layer, included by each map-enabled app's
# <App>-CMake/CMakeLists.txt with
#
#     include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../MapKit/mapkit.cmake)
#
# and folded into that app's GUI_SOURCES / GUI_INCLUDE_DIRS.
#
# Sources are listed explicitly rather than globbed. Each app's own
# touchgfx.cmake globs gui/src/**, which is right there -- everything under it
# belongs to that app. Here it would not be: a stray file in this directory
# would silently join three binaries at once, and a build that changes because
# of a file nobody added to a list is the kind of thing that is only noticed
# much later.
#
# GUI only. Nothing in MapKit belongs in a Service: verification is Map
# Manager's job, and these apps read maps solely to draw them.

set(MAPKIT_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/Sources/Container.cpp
    ${CMAKE_CURRENT_LIST_DIR}/Sources/MapSession.cpp
    ${CMAKE_CURRENT_LIST_DIR}/Sources/MapTileView.cpp
    ${CMAKE_CURRENT_LIST_DIR}/Sources/PackCatalog.cpp
    ${CMAKE_CURRENT_LIST_DIR}/Sources/PackSelection.cpp
    ${CMAKE_CURRENT_LIST_DIR}/Sources/TileRequestLog.cpp
    ${CMAKE_CURRENT_LIST_DIR}/Sources/TrackFaceMap.cpp
)

set(MAPKIT_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/Header
)
