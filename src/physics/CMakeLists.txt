# Physics layer (M11): Jolt wrapper on the starforge::jobs scheduler.

add_library(starforge_physics STATIC
    contact_listener.cpp
    contact_queue.cpp
    job_system_adapter.cpp
    physics_world.cpp
    bridge.cpp
)

target_include_directories(starforge_physics
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(starforge_physics
    PUBLIC
        starforge_jobs
        starforge_ecs
        Jolt
)

starforge_set_warnings(starforge_physics)
