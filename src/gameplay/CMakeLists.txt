# Gameplay layer (M15): damage/shields/subsystems, cargo/economy, missions,
# authored overrides, jumping and docking. Pure CPU, no renderer dependency;
# linked into the game layer and the CPU test suite.

add_library(starforge_gameplay STATIC
    damage.cpp
    economy.cpp
    missions.cpp
    progression.cpp
    authored.cpp
    jumping.cpp
    docking.cpp
)

target_include_directories(starforge_gameplay
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

starforge_set_warnings(starforge_gameplay)
