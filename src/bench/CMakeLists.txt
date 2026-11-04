add_library(starforge_bench STATIC
    benchmark_compare.cpp
    benchmark_harness.cpp
    benchmark_result.cpp
    cpu_timer.cpp
)

target_include_directories(starforge_bench
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/third_party
        ${PROJECT_SOURCE_DIR}/third_party/vma
)

target_link_libraries(starforge_bench
    PUBLIC
        starforge_core
        starforge_stats
)

starforge_set_warnings(starforge_bench)
