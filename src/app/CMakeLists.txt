set(BRANDING_DIR ${PROJECT_SOURCE_DIR}/assets/branding)

# Everything except main(), so the UI can also be driven by tests.
add_library(opendraw_ui STATIC
    CanvasView.cpp
    CanvasView.h
    CanvasWidget.cpp
    CanvasWidget.h
    ColorPalette.cpp
    ColorPalette.h
    ExportImageDialog.cpp
    ExportImageDialog.h
    FillDialog.cpp
    FillDialog.h
    Icons.cpp
    Icons.h
    Language.cpp
    Language.h
    MainWindow.cpp
    MainWindow.h
    ObjectsPanel.cpp
    ObjectsPanel.h
    PageBar.cpp
    PageBar.h
    PropertiesPanel.cpp
    PropertiesPanel.h
    PropertyBar.cpp
    PropertyBar.h
    Theme.cpp
    Theme.h
    Units.cpp
    Units.h
)
target_include_directories(opendraw_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(opendraw_ui PUBLIC opendraw_core Qt6::Widgets)

# Window icon, available at runtime as ":/opendraw-icon-256.png".
qt_add_resources(opendraw_ui "branding"
    PREFIX "/"
    BASE ${BRANDING_DIR}
    FILES ${BRANDING_DIR}/opendraw-icon-256.png
)

# Interface translations, compiled into the program as ":/i18n/opendraw_<lang>.qm".
# To refresh translations/*.ts after changing user-visible strings, run from the source folder:
#   lupdate -locations none -no-obsolete -extensions cpp,h -recursive src -ts translations/opendraw_pt_BR.ts
option(OPENDRAW_REQUIRE_TRANSLATIONS "Fail the configuration when Qt's LinguistTools are missing" OFF)
find_package(Qt6 QUIET COMPONENTS LinguistTools)
if(Qt6LinguistTools_FOUND)
    file(GLOB TS_FILES ${PROJECT_SOURCE_DIR}/translations/opendraw_*.ts)
    qt_add_lrelease(opendraw_ui TS_FILES ${TS_FILES} QM_FILES_OUTPUT_VARIABLE QM_FILES)
    qt_add_resources(opendraw_ui "translations"
        PREFIX "/i18n"
        BASE ${CMAKE_CURRENT_BINARY_DIR}
        FILES ${QM_FILES}
    )
    message(STATUS "Translations: ${TS_FILES}")
elseif(OPENDRAW_REQUIRE_TRANSLATIONS)
    message(FATAL_ERROR "Qt6 LinguistTools not found (needed for the interface translations)")
else()
    message(STATUS "Translations: disabled (Qt6 LinguistTools not found)")
endif()

set(APP_SOURCES main.cpp)

# Icon embedded in the Windows executable (shown in Explorer and the taskbar).
if(WIN32)
    set(APP_RC ${CMAKE_CURRENT_BINARY_DIR}/opendraw.rc)
    file(WRITE ${APP_RC} "IDI_ICON1 ICON \"${BRANDING_DIR}/opendraw.ico\"\n")
    list(APPEND APP_SOURCES ${APP_RC})
endif()

qt_add_executable(opendraw WIN32 MACOSX_BUNDLE ${APP_SOURCES})
target_compile_definitions(opendraw PRIVATE OPENDRAW_VERSION="${PROJECT_VERSION}")
target_link_libraries(opendraw PRIVATE opendraw_ui)

include(GNUInstallDirs)
install(TARGETS opendraw
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .
)

if(UNIX AND NOT APPLE)
    install(FILES ${PROJECT_SOURCE_DIR}/packaging/linux/opendraw.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES ${BRANDING_DIR}/opendraw-icon-256.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
            RENAME opendraw.png)
endif()
