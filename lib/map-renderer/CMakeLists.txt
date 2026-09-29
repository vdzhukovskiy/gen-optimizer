cmake_minimum_required(VERSION 3.16)

project(map-renderer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_library(map-renderer STATIC
    src/map_transform.cpp
    src/layer_item.cpp
)

add_library(map-renderer::map-renderer ALIAS map-renderer)

target_include_directories(map-renderer
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(map-renderer
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        geometry-importer::geometry-importer
)

target_compile_features(map-renderer PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(map-renderer PRIVATE /W4 /permissive-)
else()
    target_compile_options(map-renderer PRIVATE -Wall -Wextra -Wpedantic)
endif()