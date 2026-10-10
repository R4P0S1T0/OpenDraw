add_library(opendraw_core STATIC
    Commands.cpp
    Commands.h
    Document.cpp
    Document.h
    PathOps.cpp
    PathOps.h
    Shape.cpp
    Shape.h
    SvgIO.cpp
    SvgIO.h
)

target_include_directories(opendraw_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(opendraw_core PUBLIC Qt6::Core Qt6::Gui)
