# ECS scene layer (M10): Flecs world wrapper, components, spatial index.

add_library(starforge_ecs STATIC
    world.cpp
    spatial_index.cpp
    instance_stream.cpp
)

target_include_directories(starforge_ecs
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

if(TARGET flecs::flecs_static)
    target_link_libraries(starforge_ecs
        PUBLIC
            flecs::flecs_static
    )
elseif(TARGET flecs)
    target_link_libraries(starforge_ecs
        PUBLIC
            flecs
    )
endif()

# starforge_environment is PUBLIC: instance_stream.hpp exposes its types.
target_link_libraries(starforge_ecs
    PUBLIC
        starforge_math
        starforge_spatial
        starforge_environment
)

if(NOT TARGET flecs::flecs_static AND NOT TARGET flecs)
    message(FATAL_ERROR "STARFORGE_USE_FLECS is ON but no Flecs target was provided")
endif()

starforge_set_warnings(starforge_ecs)
