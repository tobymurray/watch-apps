# RecorderKit -- the four classes that put a raw sensor session on the volume,
# included by an app's CMakeLists.txt with
#
#     include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../RecorderKit/recorderkit.cmake)
#
# and folded into that app's SERVICE_SOURCES and SERVICE_INCLUDE_DIRS.
#
# Unlike HrKit this has sources, because putting bytes on a volume is not a rule
# -- it is buffering, capping, formatting and a file handle. Three of the four
# classes are SDK-free and take an injected sink, which is what lets the same
# code write to the watch and to a memory buffer in a host test; ImuFileSink is
# the one that knows about SDK::Kernel, and it exists so the other three do not.

set(RECORDERKIT_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/Sources/ImuCsvRecorder.cpp
    ${CMAKE_CURRENT_LIST_DIR}/Sources/ImuFileSink.cpp
    ${CMAKE_CURRENT_LIST_DIR}/Sources/ImuMarkerLog.cpp
    ${CMAKE_CURRENT_LIST_DIR}/Sources/HrCsvLog.cpp
)

set(RECORDERKIT_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/Header
)
