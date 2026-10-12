find_package(Qt6 REQUIRED COMPONENTS Test)

function(opendraw_add_test name)
    qt_add_executable(${name} ${name}.cpp)
    # Console programs, so test output shows up in the terminal and in CI logs.
    set_target_properties(${name} PROPERTIES WIN32_EXECUTABLE FALSE)
    target_link_libraries(${name} PRIVATE ${ARGN} Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    # Fonts and widgets need a GUI application; the offscreen platform needs no display.
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

opendraw_add_test(test_core opendraw_core)
opendraw_add_test(test_paths opendraw_core)
opendraw_add_test(test_text opendraw_core)
opendraw_add_test(test_export opendraw_core)
opendraw_add_test(test_layers opendraw_core)
opendraw_add_test(test_gradients opendraw_core)
opendraw_add_test(test_bitmaps opendraw_core)
opendraw_add_test(test_tools opendraw_core)
opendraw_add_test(test_cdr opendraw_core)
opendraw_add_test(test_blend opendraw_core)
opendraw_add_test(test_contour opendraw_core)
opendraw_add_test(test_envelope opendraw_core)
opendraw_add_test(test_extrude opendraw_core)

# Drives the drawing canvas with simulated mouse and keyboard input.
opendraw_add_test(test_canvas opendraw_ui)
