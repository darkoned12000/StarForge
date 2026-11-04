# Internal STARFORGE libraries.
#
# Layering (lower layers never depend on higher ones):
#
#   starforge_core      logging, version, foundational utilities
#   starforge_platform  SDL3 window and event handling
#   starforge_vulkan    Vulkan backend (private; never exposed in include/)
#
# `starforge` is the umbrella target applications link against.

add_subdirectory(core)
add_subdirectory(bench)
add_subdirectory(math)
add_subdirectory(spatial)
add_subdirectory(stats)
add_subdirectory(platform)
add_subdirectory(vulkan)
add_subdirectory(gpu_resources)

add_library(starforge INTERFACE)
target_link_libraries(starforge
    INTERFACE
        starforge_core
        starforge_bench
        starforge_math
        starforge_spatial
        starforge_stats
        starforge_platform
        starforge_vulkan
        starforge_gpu
)

# Public, developer-facing headers.
target_include_directories(starforge
    INTERFACE
        ${PROJECT_SOURCE_DIR}/include
)
