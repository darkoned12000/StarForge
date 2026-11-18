add_library(starforge_bench STATIC
    budget_compare.cpp
    benchmark_compare.cpp
    benchmark_harness.cpp
    benchmark_result.cpp
    benchmark_scene.cpp
    benchmark_workload.cpp
    benchmark_workload_scene.cpp
    cpu_timer.cpp
    thread_scaling.cpp
)

target_include_directories(starforge_bench
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/third_party
        ${PROJECT_SOURCE_DIR}/third_party/vma
)

target_link_libraries(starforge_bench
    PUBLIC
        Vulkan::Headers # benchmark_workload.hpp uses Vulkan value types only
        starforge_core
        starforge_content
        starforge_jobs
        starforge_planets
        starforge_math
        starforge_stats
)

starforge_set_warnings(starforge_bench)
