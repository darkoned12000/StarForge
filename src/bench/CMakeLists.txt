add_library(starforge_bench STATIC
    benchmark_result.cpp
    cpu_timer.cpp
)

target_include_directories(starforge_bench
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/third_party
)

target_link_libraries(starforge_bench
    PUBLIC
        starforge_core
        starforge_stats
)

starforge_set_warnings(starforge_bench)
