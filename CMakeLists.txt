cmake_minimum_required(VERSION 3.21)

project(OpenDraw
    VERSION 0.2.0
    DESCRIPTION "Free, open-source, cross-platform vector graphics editor"
    HOMEPAGE_URL "https://github.com/R4P0S1T0/OpenDraw"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets)
qt_standard_project_setup()

include(CTest)

add_subdirectory(src/core)
add_subdirectory(src/app)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
