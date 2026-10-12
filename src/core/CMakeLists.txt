add_library(opendraw_core STATIC
    Arrange.cpp
    Arrange.h
    Blend.cpp
    Blend.h
    Commands.cpp
    Commands.h
    ContourEffect.cpp
    ContourEffect.h
    Document.cpp
    Document.h
    Envelope.cpp
    Envelope.h
    Export.cpp
    Export.h
    Generators.cpp
    Generators.h
    PathOps.cpp
    PathOps.h
    Render.cpp
    Render.h
    RichText.cpp
    RichText.h
    Shape.cpp
    Shape.h
    SvgIO.cpp
    SvgIO.h
    TextLayout.cpp
    TextLayout.h
)

target_include_directories(opendraw_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(opendraw_core PUBLIC Qt6::Core Qt6::Gui)

# CorelDRAW import through libcdr (MPL-2.0). Optional: without it OpenDraw builds and runs,
# it just cannot open .cdr files.
option(OPENDRAW_WITH_LIBCDR "Import CorelDRAW files with libcdr" ON)
option(OPENDRAW_REQUIRE_LIBCDR "Fail the configuration when libcdr is missing (release builds)" OFF)
target_sources(opendraw_core PRIVATE CdrImport.cpp CdrImport.h)
if(OPENDRAW_WITH_LIBCDR OR OPENDRAW_REQUIRE_LIBCDR)
    find_package(PkgConfig)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(LIBCDR IMPORTED_TARGET libcdr-0.1 librevenge-0.0 librevenge-generators-0.0
                          librevenge-stream-0.0)
    endif()
    if(LIBCDR_FOUND)
        target_link_libraries(opendraw_core PRIVATE PkgConfig::LIBCDR)
        target_compile_definitions(opendraw_core PRIVATE OPENDRAW_HAVE_LIBCDR)
        message(STATUS "CorelDRAW import: enabled (libcdr ${LIBCDR_libcdr-0.1_VERSION})")
    elseif(OPENDRAW_REQUIRE_LIBCDR)
        message(FATAL_ERROR "libcdr not found (install libcdr and librevenge development packages)")
    else()
        message(STATUS "CorelDRAW import: disabled (libcdr not found)")
    endif()
endif()
