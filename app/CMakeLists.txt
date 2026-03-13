cmake_minimum_required(VERSION 3.16)
project(LogosStandaloneApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets RemoteObjects)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets RemoteObjects)

if(NOT DEFINED LOGOS_LIBLOGOS_ROOT)
    message(FATAL_ERROR "LOGOS_LIBLOGOS_ROOT must be defined")
endif()

if(NOT DEFINED LOGOS_CPP_SDK_ROOT)
    message(FATAL_ERROR "LOGOS_CPP_SDK_ROOT must be defined")
endif()

message(STATUS "logos-liblogos : ${LOGOS_LIBLOGOS_ROOT}")
message(STATUS "logos-cpp-sdk  : ${LOGOS_CPP_SDK_ROOT}")

include_directories(
    ${LOGOS_LIBLOGOS_ROOT}/include
    ${LOGOS_CPP_SDK_ROOT}/include
    ${LOGOS_CPP_SDK_ROOT}/include/cpp
    ${LOGOS_CPP_SDK_ROOT}/include/core
)

link_directories(
    ${LOGOS_LIBLOGOS_ROOT}/lib
    ${LOGOS_CPP_SDK_ROOT}/lib
)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(logos-standalone
    main.cpp
    mainwindow.cpp
    mainwindow.h
)

target_link_libraries(logos-standalone PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::RemoteObjects
    logos_core
    logos_sdk
)

if(APPLE)
    set_target_properties(logos-standalone PROPERTIES
        INSTALL_RPATH "@executable_path/../lib"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
elseif(UNIX)
    set_target_properties(logos-standalone PROPERTIES
        INSTALL_RPATH "$ORIGIN/../lib"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

install(TARGETS logos-standalone RUNTIME DESTINATION bin)
