# ECS scene layer (M10): Flecs world wrapper, components, spatial index.
# (Physics bridge lives in starforge_physics to avoid a dependency cycle.)

add_library(starforge_ecs STATIC
    world.cpp
    spatial_index.cpp
    instance_stream.cpp
    content_instantiation.cpp
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

# M5 Phase 1: instance_stream.hpp exposes environment::AsteroidInstance, a
# plain header-only struct. Depend on the headers (Vulkan::Headers) rather than
# the environment library so CPU-only consumers do not inherit the Vulkan
# loader through starforge_environment -> starforge_gpu -> starforge_vulkan.
target_link_libraries(starforge_ecs
    PUBLIC
        Vulkan::Headers
        starforge_math
        starforge_spatial
        starforge_content
)

if(NOT TARGET flecs::flecs_static AND NOT TARGET flecs)
    message(FATAL_ERROR "STARFORGE_USE_FLECS is ON but no Flecs target was provided")
endif()

starforge_set_warnings(starforge_ecs)
