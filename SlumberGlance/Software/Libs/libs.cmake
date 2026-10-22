file(GLOB_RECURSE LIBS_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_LIST_DIR}/Sources/*.cpp
)

# The card's format and store are Slumber's; this app only reads them.
set(LIBS_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/Header
    ${CMAKE_CURRENT_LIST_DIR}/../../../Slumber/Software/Libs/Header/Card
)
