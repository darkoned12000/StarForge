# Audio layer (M12): miniaudio wrapper.
#
# Layering: starforge_audio sits above starforge_core (logging) and
# depends only on the miniaudio target. It is a game-layer library
# (like starforge_ecs/starforge_physics): internal to src/, never
# exposed through include/starforge/ until M12.5 (public facade).

add_library(starforge_audio STATIC
    audio_context.cpp
    sound.cpp
    tone_generator.cpp
)

target_include_directories(starforge_audio
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(starforge_audio
    PUBLIC
        starforge_core
        miniaudio
)

if(NOT TARGET miniaudio)
    message(FATAL_ERROR "STARFORGE_USE_MINIAUDIO is ON but no miniaudio target was provided")
endif()

starforge_set_warnings(starforge_audio)
