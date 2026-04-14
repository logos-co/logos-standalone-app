cmake_minimum_required(VERSION 3.16)
project(LogosStandaloneApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets RemoteObjects Quick QuickWidgets QuickControls2 Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets RemoteObjects Quick QuickWidgets QuickControls2 Network)

# QML Inspector (Qt DevTools-like inspection over TCP)
# Disabled by default. Pass -DENABLE_QML_INSPECTOR=ON to enable.
# The nix build enables it automatically when logos-qt-mcp is available.
if(NOT DEFINED ENABLE_QML_INSPECTOR)
    set(ENABLE_QML_INSPECTOR OFF)
endif()

if(ENABLE_QML_INSPECTOR)
    if(NOT DEFINED LOGOS_QT_MCP_ROOT)
        get_filename_component(LOGOS_QT_MCP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../logos-qt-mcp" REALPATH)
    endif()
    if(NOT EXISTS "${LOGOS_QT_MCP_ROOT}/qt-plugin/CMakeLists.txt")
        message(FATAL_ERROR "logos-qt-mcp not found at ${LOGOS_QT_MCP_ROOT}. "
                            "Set LOGOS_QT_MCP_ROOT or clone https://github.com/logos-co/logos-qt-mcp")
    endif()
    add_subdirectory("${LOGOS_QT_MCP_ROOT}/qt-plugin" "${CMAKE_CURRENT_BINARY_DIR}/qml-inspector")
endif()

if(NOT DEFINED LOGOS_LIBLOGOS_ROOT)
    message(FATAL_ERROR "LOGOS_LIBLOGOS_ROOT must be defined")
endif()

if(NOT DEFINED LOGOS_CPP_SDK_ROOT)
    message(FATAL_ERROR "LOGOS_CPP_SDK_ROOT must be defined")
endif()

if(NOT DEFINED LOGOS_VIEW_MODULE_RUNTIME_ROOT)
    message(FATAL_ERROR "LOGOS_VIEW_MODULE_RUNTIME_ROOT must be defined")
endif()

message(STATUS "logos-liblogos   : ${LOGOS_LIBLOGOS_ROOT}")
message(STATUS "logos-cpp-sdk    : ${LOGOS_CPP_SDK_ROOT}")
message(STATUS "logos-view-module-runtime : ${LOGOS_VIEW_MODULE_RUNTIME_ROOT}")

include_directories(
    ${LOGOS_LIBLOGOS_ROOT}/include
    ${LOGOS_CPP_SDK_ROOT}/include
    ${LOGOS_CPP_SDK_ROOT}/include/cpp
    ${LOGOS_CPP_SDK_ROOT}/include/core
    ${LOGOS_VIEW_MODULE_RUNTIME_ROOT}/include
)

link_directories(
    ${LOGOS_LIBLOGOS_ROOT}/lib
    ${LOGOS_CPP_SDK_ROOT}/lib
    ${LOGOS_VIEW_MODULE_RUNTIME_ROOT}/lib
)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(logos-standalone-app
    main.cpp
    mainwindow.cpp
    mainwindow.h
)

if(ENABLE_QML_INSPECTOR)
    target_compile_definitions(logos-standalone-app PRIVATE ENABLE_QML_INSPECTOR)
endif()

target_link_libraries(logos-standalone-app PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::RemoteObjects
    Qt${QT_VERSION_MAJOR}::Quick
    Qt${QT_VERSION_MAJOR}::QuickWidgets
    Qt${QT_VERSION_MAJOR}::QuickControls2
    Qt${QT_VERSION_MAJOR}::Network
    logos_view_module_runtime
    logos_core
    logos_sdk
    $<$<BOOL:${ENABLE_QML_INSPECTOR}>:qml-inspector>
)

if(APPLE)
    set_target_properties(logos-standalone-app PROPERTIES
        INSTALL_RPATH "@executable_path/../lib"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
elseif(UNIX)
    set_target_properties(logos-standalone-app PROPERTIES
        INSTALL_RPATH "$ORIGIN/../lib"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

install(TARGETS logos-standalone-app RUNTIME DESTINATION bin)
