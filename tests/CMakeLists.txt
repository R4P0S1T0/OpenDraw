find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(test_core test_core.cpp)
target_link_libraries(test_core PRIVATE opendraw_core Qt6::Test)

add_test(NAME test_core COMMAND test_core)
