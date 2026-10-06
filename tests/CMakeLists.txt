add_executable(cancellation-checks
    cancellation_checks.cpp
    ../mainwindow.cpp
    ../mainwindow.h
    ../app/panels/algorithm_panel.cpp
    ../app/panels/algorithm_panel.h
    ../app/panels/data_panel.cpp
    ../app/panels/data_panel.h
    ../app/panels/status_panel.cpp
    ../app/panels/status_panel.h
)
target_include_directories(cancellation-checks PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(cancellation-checks PRIVATE core map-renderer::map-renderer Qt6::Widgets)
add_test(NAME cancellation-checks COMMAND cancellation-checks)
set_tests_properties(cancellation-checks PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 60
)

add_executable(metrics-checks metrics_checks.cpp)
target_link_libraries(metrics-checks PRIVATE core)
add_test(NAME metrics-checks COMMAND metrics-checks)
set_tests_properties(metrics-checks PROPERTIES TIMEOUT 60)
