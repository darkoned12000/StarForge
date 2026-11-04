add_library(starforge_stats STATIC
    statistics.cpp
)

target_include_directories(starforge_stats
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(starforge_stats
    PUBLIC
        starforge_core
)

starforge_set_warnings(starforge_stats)
