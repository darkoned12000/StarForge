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

# GPU-driven benchmark workload is a separate target so the CPU-only test
# binary never inherits the Vulkan loader through it (M5 Phase 1 gate).
add_library(starforge_gpu_driven_bench STATIC
    gpu_driven_workload.cpp
)

target_include_directories(starforge_gpu_driven_bench
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/third_party/vma
)

target_link_libraries(starforge_gpu_driven_bench
    PUBLIC
        starforge_bench
        starforge_gpu
        starforge_vulkan
        starforge_math
)

starforge_set_warnings(starforge_gpu_driven_bench)

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
        starforge_vulkan_queue
        starforge_content
        starforge_jobs
        starforge_planets
        starforge_math
        starforge_stats
)

starforge_set_warnings(starforge_bench)
