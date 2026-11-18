add_executable(starforge_scaling_bench main.cpp)

target_link_libraries(starforge_scaling_bench
    PRIVATE
        starforge_bench
        starforge_content
        starforge_jobs
        starforge_core
)

starforge_set_warnings(starforge_scaling_bench)
