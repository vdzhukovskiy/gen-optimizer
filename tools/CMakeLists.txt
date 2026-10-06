find_package(GDAL REQUIRED)
add_executable(prepare-scenarios prepare_scenarios.cpp)
target_link_libraries(prepare-scenarios PRIVATE GDAL::GDAL Qt6::Core core)
