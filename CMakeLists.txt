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

# --- GDAL -------------------------------------------------------------
find_package(GDAL REQUIRED)

# --- Target -----------------------------------------------------------
add_executable(gen-optimizer
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
)

target_link_libraries(gen-optimizer PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    GDAL::GDAL
)