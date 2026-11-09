add_library(starforge_content STATIC
    content_generation.cpp
    cell_traversal.cpp
    lod_transition.cpp
    player_delta.cpp
)

target_include_directories(starforge_content
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(starforge_content
    PUBLIC
        starforge_core
        starforge_jobs
        starforge_math
        starforge_spatial
)

starforge_set_warnings(starforge_content)

# Determinism: do not let the compiler contract a*b+c into an FMA. FMA
# availability differs between x86-64 and ARM, which would make generated
# content differ across machines that otherwise follow IEEE-754.
if(MSVC)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(starforge_content PRIVATE -ffp-contract=off)
    else()
        target_compile_options(starforge_content PRIVATE /fp:precise)
    endif()
else()
    target_compile_options(starforge_content PRIVATE -ffp-contract=off)
endif()