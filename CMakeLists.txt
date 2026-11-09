cmake_minimum_required(VERSION 3.16)

project(MessengerApp
    VERSION 2.8.5
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

# OpenSSL 1.1.1 нужен QSslSocket из Qt 5.14; раскладываем рядом с бинарниками.
if(WIN32)
    set(MESSENGER_OPENSSL_DLLS
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/openssl/win64/libcrypto-1_1-x64.dll
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/openssl/win64/libssl-1_1-x64.dll)
endif()

# Переводы: .ts → .qm рядом с исполняемым файлом.
find_package(Qt5 5.14 QUIET COMPONENTS LinguistTools)
if(Qt5LinguistTools_FOUND)
    qt5_add_translation(MESSENGER_QM
        ${CMAKE_CURRENT_SOURCE_DIR}/translations/messenger_en.ts)

    add_custom_target(translations ALL DEPENDS ${MESSENGER_QM})
    add_custom_command(TARGET translations POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/translations
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MESSENGER_QM}
                ${CMAKE_BINARY_DIR}/translations
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/tests/translations
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MESSENGER_QM}
                ${CMAKE_BINARY_DIR}/tests/translations)
else()
    message(STATUS "Qt LinguistTools не найдены: английский перевод не будет собран")
endif()

include(CTest)

add_subdirectory(src)

if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    find_package(Qt5 5.14 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()

# ── Установка ──────────────────────────────────────────────────────────────
install(TARGETS MessengerClient MessengerServer
        RUNTIME DESTINATION bin)
