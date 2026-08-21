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

if(NOT DEFINED LOGOS_QT_HOST_ROOT)
    message(FATAL_ERROR "LOGOS_QT_HOST_ROOT must be defined")
endif()

if(NOT DEFINED LOGOS_PROTOCOL_ROOT)
    message(FATAL_ERROR "LOGOS_PROTOCOL_ROOT must be defined")
endif()

if(NOT DEFINED LOGOS_VIEW_MODULE_RUNTIME_ROOT)
    message(FATAL_ERROR "LOGOS_VIEW_MODULE_RUNTIME_ROOT must be defined")
endif()

message(STATUS "logos-liblogos   : ${LOGOS_LIBLOGOS_ROOT}")
message(STATUS "logos-cpp-sdk    : ${LOGOS_CPP_SDK_ROOT}")
message(STATUS "logos-qt-host    : ${LOGOS_QT_HOST_ROOT}")
message(STATUS "logos-protocol   : ${LOGOS_PROTOCOL_ROOT}")
message(STATUS "logos-view-module-runtime : ${LOGOS_VIEW_MODULE_RUNTIME_ROOT}")

# Link the SDK stack via exported CMake targets so this consumer inherits
# INTERFACE_INCLUDE_DIRECTORIES + INTERFACE_LINK_LIBRARIES (OpenSSL::SSL,
# OpenSSL::Crypto, Boost::system, nlohmann_json) — a bare archive on the
# link line would leave every Boost.Asio TLS symbol undefined. Since the
# qt split the Qt HOST RUNTIME (LogosAPI, provider objects) lives in
# logos-plugin-qt as logos-qt-host, which carries the logos-protocol library
# transitively; the Qt-free base SDK contributes header-only types via
# logos_headers.
find_package(logos-protocol REQUIRED CONFIG
    PATHS ${LOGOS_PROTOCOL_ROOT}/lib/cmake/logos-protocol
    NO_DEFAULT_PATH)
find_package(logos-qt-host REQUIRED CONFIG
    PATHS ${LOGOS_QT_HOST_ROOT}/lib/cmake/logos-qt-host
    NO_DEFAULT_PATH)
find_package(logos-cpp-sdk REQUIRED CONFIG
    PATHS ${LOGOS_CPP_SDK_ROOT}/lib/cmake/logos-cpp-sdk
    NO_DEFAULT_PATH)
find_package(LogosDesignSystem CONFIG REQUIRED)

# NOTE: logos-liblogos re-exports its own copy of the host-runtime headers
# (logos_api.h & co.) into ${LOGOS_LIBLOGOS_ROOT}/include, and this explicit -I
# precedes the -isystem that nix's cc-wrapper adds for the logos-qt-host
# buildInput. So the app LINKS logos-qt-host::logos_qt_host_shared but COMPILES those
# headers out of liblogos' re-export. Listing ${LOGOS_QT_HOST_ROOT}/include here
# does NOT change that — CMake strips any dir it sees in
# CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES, which is exactly where the wrapper put
# it. The two copies agree as long as liblogos is built against the same qt-host,
# which is the only pin set this app is meant to be built in.
include_directories(
    ${LOGOS_LIBLOGOS_ROOT}/include
    ${LOGOS_VIEW_MODULE_RUNTIME_ROOT}/include
)

link_directories(
    ${LOGOS_LIBLOGOS_ROOT}/lib
    ${LOGOS_VIEW_MODULE_RUNTIME_ROOT}/lib
)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(logos-standalone-app
    main.cpp
    mainwindow.cpp
    mainwindow.h
    QmlLiveView.cpp
    QmlLiveView.h
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
    # The SHARED Qt host runtime, not the static archive.
    #
    # This app loads third-party plugins IN-PROCESS (QPluginLoader, see
    # mainwindow.cpp's loadLegacyWidget / apiForPlugin), so the types that must
    # exist exactly once per process -- TokenManager, the per-identity
    # StoreRegistry, LogosAPI -- must come from ONE image. Linking the static
    # archive gave this exe its own copy of every function-local static inside
    # it, so a token the host saved was invisible to the plugin and every
    # cross-module call was refused, with no build diagnostic.
    #
    # It became load-bearing rather than merely untidy with logos-liblogos#182:
    # liblogos_core no longer DEFINES the runtime, it imports it. An exe still
    # linking the archive would therefore hold the only other copy in the
    # process, which is the split-brain rather than a spare.
    #
    # logos_qt_host_shared PUBLIC-links logos_protocol_shared, so the protocol
    # half arrives transitively and correctly layered.
    logos-qt-host::logos_qt_host_shared
    logos-cpp-sdk::logos_headers
    Logos::DesignSystem
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
