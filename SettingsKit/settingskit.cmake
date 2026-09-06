# SettingsKit -- the shared watch-settings mechanism, included by each app that
# reads or writes the kernel's live settings struct, with
#
#     include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../SettingsKit/settingskit.cmake)
#
# and folded into that app's GUI_SOURCES / GUI_INCLUDE_DIRS.
#
# Sources are listed explicitly rather than globbed: a stray file here would
# silently join every app that includes this, and a build that changes because
# of a file nobody added to a list is noticed much later than it should be.
#
# GUI only, and deliberately so. Every one of these files either carries a raw
# firmware address or is reached by something that does, and a Service that
# neither reads nor writes the setting has no business linking one in. Apps here
# keep their Service halves out of this list.

set(SETTINGSKIT_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/Sources/DebugLog.cpp
    ${CMAKE_CURRENT_LIST_DIR}/Sources/FirmwareGate.cpp
    ${CMAKE_CURRENT_LIST_DIR}/Sources/LiveSettings.cpp
    ${CMAKE_CURRENT_LIST_DIR}/Sources/SettingsAddresses.cpp
    ${CMAKE_CURRENT_LIST_DIR}/Sources/SettingsPersist.cpp
)

set(SETTINGSKIT_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/Header
)

# DebugLog.cpp is the one file a Service half does want, since a Service logs
# too and it carries no address of its own.
set(SETTINGSKIT_DEBUGLOG_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/Sources/DebugLog.cpp
)
