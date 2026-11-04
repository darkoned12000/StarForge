add_library(starforge_spatial STATIC
    world_position.cpp
    camera_relative.cpp
    render_object.cpp
    world_position.hpp
    camera_relative.hpp
    render_object.hpp
)

target_compile_features(starforge_spatial PUBLIC cxx_std_23)

target_include_directories(starforge_spatial
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(starforge_spatial
    PUBLIC
        starforge_core
        starforge_math
)

starforge_set_warnings(starforge_spatial)
