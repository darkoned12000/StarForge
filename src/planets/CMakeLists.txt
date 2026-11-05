add_library(starforge_planets STATIC
    cubed_sphere.cpp
    cubed_sphere.hpp
    planet_quadtree.cpp
    planet_quadtree.hpp
    planet_patch.cpp
    planet_patch.hpp
    terrain.cpp
    terrain.hpp
    atmosphere.cpp
    atmosphere.hpp
)

target_include_directories(starforge_planets
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(starforge_planets
    PUBLIC
        starforge_core
        starforge_math
        starforge_spatial
)

target_compile_features(starforge_planets PUBLIC cxx_std_20)

starforge_set_warnings(starforge_planets)

# Determinism: do not let the compiler contract a*b+c into an FMA.
if(NOT MSVC)
    target_compile_options(starforge_planets PRIVATE -ffp-contract=off)
endif()
