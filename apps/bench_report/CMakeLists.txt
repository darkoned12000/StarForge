add_executable(starforge_bench_report main.cpp)

target_link_libraries(starforge_bench_report
    PRIVATE
        starforge_bench
        starforge_core
)

starforge_set_warnings(starforge_bench_report)
