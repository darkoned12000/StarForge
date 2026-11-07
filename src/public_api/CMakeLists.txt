# Public API implementation (facades over the internal libraries).

add_library(starforge_public_api STATIC
    application.cpp
    renderer.cpp
    starfield.cpp
    starfield_bridge.cpp
    world.cpp
    world_bridge.cpp
)

target_compile_definitions(starforge_public_api
    PRIVATE
        STARFORGE_SHADER_DIR="${PROJECT_BINARY_DIR}/shaders"
)

target_include_directories(starforge_public_api
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(starforge_public_api
    PRIVATE
        starforge_platform
        starforge_vulkan
        starforge_spatial
        starforge_environment
)

starforge_set_warnings(starforge_public_api)
