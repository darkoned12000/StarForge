add_library(starforge_planets STATIC
    cubed_sphere.cpp
    cubed_sphere.hpp
    planet_quadtree.cpp
    planet_quadtree.hpp
    planet_patch.cpp
    planet_patch.hpp
    planet_mesh.cpp
    planet_mesh.hpp
    terrain.cpp
    terrain.hpp
    atmosphere.cpp
    atmosphere.hpp
)

target_include_directories(starforge_planets
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(starforge_planets
    PUBLIC
        starforge_core
        starforge_math
        starforge_spatial
        starforge_jobs
)

target_compile_features(starforge_planets PUBLIC cxx_std_20)

starforge_set_warnings(starforge_planets)

# Determinism: do not let the compiler contract a*b+c into an FMA.
if(MSVC)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(starforge_planets PRIVATE -ffp-contract=off)
    else()
        target_compile_options(starforge_planets PRIVATE /fp:contract-)
    endif()
else()
    target_compile_options(starforge_planets PRIVATE -ffp-contract=off)
endif()