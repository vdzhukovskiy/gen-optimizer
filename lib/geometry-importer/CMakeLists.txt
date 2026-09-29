cmake_minimum_required(VERSION 3.16)

project(geometry-importer
    VERSION 0.1.0
    DESCRIPTION "Iterate over vector map geometry"
    LANGUAGES CXX
)

# --- Global settings -------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static by default, but let a parent project override via BUILD_SHARED_LIBS
if(NOT DEFINED BUILD_SHARED_LIBS)
    set(BUILD_SHARED_LIBS OFF)
endif()

# Needed if this static lib is ever linked into a shared library downstream
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# --- Dependencies ----------------------------------------------------
find_package(GDAL REQUIRED)

# --- Library ---------------------------------------------------------
add_library(geometry-importer STATIC
    src/importer.cpp
    src/geometry.cpp
)

# Namespaced alias: consumers always link geometry-importer::geometry-importer
add_library(geometry-importer::geometry-importer ALIAS geometry-importer)

target_include_directories(geometry-importer
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# GDAL is an implementation detail — do NOT propagate it to consumers
target_link_libraries(geometry-importer
    PRIVATE
        GDAL::GDAL
)

target_compile_features(geometry-importer PUBLIC cxx_std_20)

# Warnings (only on this target, never as global CMAKE_CXX_FLAGS)
if(MSVC)
    target_compile_options(geometry-importer PRIVATE /W4 /permissive-)
else()
    target_compile_options(geometry-importer PRIVATE -Wall -Wextra -Wpedantic)
endif()

# --- Tests (opt-in) --------------------------------------------------
option(GEOMETRY_IMPORTER_BUILD_TESTS "Build geometry-importer tests" OFF)
if(GEOMETRY_IMPORTER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()