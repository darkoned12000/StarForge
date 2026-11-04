add_library(starforge_spatial STATIC
    world_position.cpp
    camera_relative.cpp
    render_object.cpp
)

target_include_directories(starforge_spatial
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(starforge_spatial
    PUBLIC
        starforge_core
        starforge_math
)

starforge_set_warnings(starforge_spatial)
