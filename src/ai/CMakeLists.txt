# AI and behavior layer (M14): components, perception, and steering.
#
# The layer lives in the game layer, not behind the public API: AI state
# is an internal detail of simulation entities until a public consumer
# justifies a facade (ADR-0009).

add_library(starforge_ai STATIC
    components.cpp
    perception.cpp
    steering.cpp
)

target_include_directories(starforge_ai
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(starforge_ai
    PUBLIC
        starforge_ecs
        starforge_math
        starforge_spatial
)

starforge_set_warnings(starforge_ai)
