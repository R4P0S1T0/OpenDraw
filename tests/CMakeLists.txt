find_package(Qt6 REQUIRED COMPONENTS Test)

foreach(name test_core test_paths)
    qt_add_executable(${name} ${name}.cpp)
    # Console programs, so test output shows up in the terminal and in CI logs.
    set_target_properties(${name} PROPERTIES WIN32_EXECUTABLE FALSE)
    target_link_libraries(${name} PRIVATE opendraw_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endforeach()
