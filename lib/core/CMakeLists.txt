set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)


add_library(core STATIC
    include/core/session.h
    include/core/map_data.h
    src/session.cpp
)

target_include_directories(core PUBLIC include)
target_link_libraries(core PUBLIC
    geometry-importer
    Qt6::Core
)

set_target_properties(core PROPERTIES AUTOMOC ON)