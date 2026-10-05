cmake_minimum_required(VERSION 3.16)

project(gen-optimizer VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt automoc/autouic/autorcc (Qt6-friendly; also fine on Qt5)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# --- Qt ---------------------------------------------------------------
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

# --- Target -----------------------------------------------------------
add_executable(gen-optimizer
    main.cpp
    mainwindow.cpp
    mainwindow.h

    app/panels/data_panel.cpp
    app/panels/data_panel.h
    app/panels/algorithm_panel.cpp
    app/panels/algorithm_panel.h
    app/panels/status_panel.cpp
    app/panels/status_panel.h
)

target_link_libraries(gen-optimizer PRIVATE
    Qt6::Widgets
    geometry-importer::geometry-importer
    map-renderer::map-renderer
    core
)

target_include_directories(gen-optimizer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_subdirectory(lib)

option(GEN_OPTIMIZER_BUILD_TESTS "Build cancellation and asynchronous UI checks" OFF)
if(GEN_OPTIMIZER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
