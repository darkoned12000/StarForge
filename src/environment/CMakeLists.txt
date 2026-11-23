# Environment effects module

add_library(starforge_environment STATIC
    internal/effect_pipeline.cpp
    internal/texture_effect.cpp
    nebula_renderer.cpp
    dust_field.cpp
    particle_system.cpp
    starfield_renderer.cpp
    thruster_effect.cpp
    weapon_effect.cpp
    collision_effect.cpp
    sun_distortion.cpp
    wormhole_effect.cpp
    ftl_effect.cpp
    afterburner_effect.cpp
    storm_effect.cpp
    shield_effect.cpp
    asteroid_field.cpp
    toon_renderer.cpp
    hud_renderer.cpp
    sun_renderer.cpp
    planet_renderer.cpp
    rock_field_renderer.cpp
    combat_fx_renderer.cpp
    nebula_optimized.cpp
    ui_renderer.cpp
)

target_include_directories(starforge_environment PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(starforge_environment PRIVATE
    starforge_core
    starforge_math
    starforge_gpu
    starforge_vulkan
    starforge_planets
    starforge_spatial
)

starforge_set_warnings(starforge_environment)
