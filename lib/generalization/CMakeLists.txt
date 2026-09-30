add_library(generalization STATIC
    src/algorithm.cpp
    src/detail/geometry_ops.cpp
    src/detail/param_utils.cpp
    src/algorithms/douglas_peucker.cpp
    src/algorithms/visvalingam_whyatt.cpp
    src/algorithms/li_openshaw.cpp
)

target_include_directories(generalization PUBLIC include)
target_compile_features(generalization PUBLIC cxx_std_17)
target_link_libraries(generalization PUBLIC geometry-importer)