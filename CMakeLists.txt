cmake_minimum_required(VERSION 3.14)
project(Lux VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(BUILD_CLIENT "Build the lux CLI" ON)
option(BUILD_TESTS "Build test programs" OFF)
option(LUX_ENABLE_WIFI "Enable WiFi reconnection (needs libnm on Linux)" OFF)
option(LUX_ENABLE_CURL "Enable libcurl backend" ON)

# ---------------------------------------------------------------------------
# Find dependencies
# ---------------------------------------------------------------------------
find_package(Threads REQUIRED)

# Optional: libcurl for HTTP transfers (strongly recommended)
find_package(CURL QUIET)

# ---------------------------------------------------------------------------
# Targets (static libraries)
# ---------------------------------------------------------------------------
add_library(lux_io STATIC
    src/io/buffer.cpp
    src/io/filesystem_utils.cpp
    src/io/state_file.cpp
)
target_include_directories(lux_io PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

add_library(lux_net STATIC
    src/net/error.cpp
    src/net/backend.cpp
    src/net/http_client.cpp
    src/net/workers.cpp
    src/net/remote_folder_parser.cpp
)

target_include_directories(lux_net PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

if(CURL_FOUND)
    target_compile_definitions(lux_net PRIVATE LUX_HAVE_CURL)
    target_sources(lux_net PRIVATE
        src/net/curl_backend.cpp
    )
    message(STATUS "Found libcurl: ${CURL_LIBRARIES}")
else()
    message(STATUS "libcurl not found - HTTP transfers will be stubbed")
endif()

add_library(lux_engine STATIC
    src/engine/types.cpp
    src/engine/progress_tracker.cpp
    src/engine/dispatcher.cpp
)
target_include_directories(lux_engine PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

add_library(lux_cli STATIC
    src/cli/renderer.cpp
)
target_include_directories(lux_cli PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

add_library(lux_wifi STATIC
    src/wifi/manager.cpp
)
target_include_directories(lux_wifi PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# ---------------------------------------------------------------------------
# Main executable
# ---------------------------------------------------------------------------
add_executable(lux
    src/main.cpp
)

target_link_libraries(lux
    PRIVATE
        lux_cli
        lux_wifi
        lux_engine
        lux_net
        lux_io
        Threads::Threads
)

if(CURL_FOUND)
    target_link_libraries(lux PRIVATE CURL::libcurl)
endif()

if(LUX_ENABLE_WIFI AND UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    pkg_check_modules(LIBNM libnm>=1.0.0 QUIET)
    if(LIBNM_FOUND)
        target_link_libraries(lux PRIVATE ${LIBNM_LIBRARIES})
        target_compile_definitions(lux PRIVATE LUX_HAS_NETWORKMANAGER)
    endif()
endif()

target_include_directories(lux PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
