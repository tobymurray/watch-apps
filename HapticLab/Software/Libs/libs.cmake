# Enumerated rather than globbed, because the two halves own different sources
# here: the GUI owns the protocol and the run files, the Service owns Phase 1.
# A glob would put each half's sources in the other's link.
set(LIBS_SOURCES_SERVICE
    ${CMAKE_CURRENT_LIST_DIR}/Sources/Service.cpp
)

set(LIBS_SOURCES_GUI
    ${CMAKE_CURRENT_LIST_DIR}/Sources/Recorder.cpp
)

# Header/Analysis is deliberately absent: the statistics are host-only double
# arithmetic and neither watch ELF includes them.
set(LIBS_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/Header
)
