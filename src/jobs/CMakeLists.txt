add_library(starforge_jobs STATIC
    job_scheduler.cpp
)

target_include_directories(starforge_jobs
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(starforge_jobs
    PUBLIC
        starforge_core
)

starforge_set_warnings(starforge_jobs)
