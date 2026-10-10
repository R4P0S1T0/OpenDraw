qt_add_executable(opendraw WIN32 MACOSX_BUNDLE
    CanvasWidget.cpp
    CanvasWidget.h
    ColorPalette.cpp
    ColorPalette.h
    MainWindow.cpp
    MainWindow.h
    PropertyBar.cpp
    PropertyBar.h
    main.cpp
)

target_compile_definitions(opendraw PRIVATE OPENDRAW_VERSION="${PROJECT_VERSION}")
target_link_libraries(opendraw PRIVATE opendraw_core Qt6::Widgets)

include(GNUInstallDirs)
install(TARGETS opendraw
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .
)
