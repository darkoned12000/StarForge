# First Flight (M15.0): playable demo, `apps/game/`.
# Game-layer executable: rendering via the public API, simulation via
# the internal ECS/physics/AI libs (M10+).

add_executable(starforge_game
    main.cpp
    game_bridge.cpp
)

target_link_libraries(starforge_game
    PRIVATE
        starforge_public_api
        starforge_ecs
        starforge_math
        starforge_spatial
)

starforge_set_warnings(starforge_game)

if(TARGET starforge_shaders)
    add_dependencies(starforge_game starforge_shaders)
endif()
