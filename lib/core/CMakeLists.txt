set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)


add_library(core STATIC
    include/core/session.h
    include/core/map_data.h
    include/core/scenario.h
    src/scenario.cpp
    include/core/generalization_worker.h
    src/generalization_worker.cpp
    src/session.cpp
    src/metrics.cpp
    include/core/run_history.h
    src/run_history.cpp
)

target_include_directories(core PUBLIC include)
target_link_libraries(core PUBLIC
    geometry-importer
    generalization
    Qt6::Core
)

set_target_properties(core PROPERTIES AUTOMOC ON)
