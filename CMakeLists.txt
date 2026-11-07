cmake_minimum_required(VERSION 3.16)

project(MessengerApp
    VERSION 1.0.0
    DESCRIPTION "Cross-platform desktop messenger built with Qt Widgets"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus)
else()
    add_compile_options(-Wall -Wextra)
endif()

find_package(Qt5 5.14 REQUIRED COMPONENTS
    Core Gui Widgets Network Sql Multimedia MultimediaWidgets Concurrent Svg)

include(CTest)

add_subdirectory(src)

if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    find_package(Qt5 5.14 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()

# ── Установка ──────────────────────────────────────────────────────────────
install(TARGETS MessengerClient MessengerServer
        RUNTIME DESTINATION bin)
