add_library(starforge_vulkan_core STATIC
    vulkan_capabilities.cpp
)

target_include_directories(starforge_vulkan_core PUBLIC ${PROJECT_SOURCE_DIR}/src)

target_link_libraries(starforge_vulkan_core
    PUBLIC
        starforge_core
)

starforge_set_warnings(starforge_vulkan_core)

add_library(starforge_vulkan STATIC
    vulkan_context.cpp
    vulkan_device.cpp
    vulkan_physical_device.cpp
    vulkan_pipeline.cpp
    vulkan_renderer.cpp
    threaded_command_pools.cpp
    vulkan_swapchain.cpp
    vulkan_timestamps.cpp
)

target_include_directories(starforge_vulkan PUBLIC ${PROJECT_SOURCE_DIR}/src)

target_link_libraries(starforge_vulkan
    PUBLIC
        starforge_core
        starforge_vulkan_core
        Vulkan::Vulkan
)

# PUBLIC on purpose: vulkan_context.hpp reads this macro, so every consumer
# must see the same value (avoids ODR mismatches). Debug builds only.
target_compile_definitions(starforge_vulkan
    PUBLIC
        $<$<AND:$<BOOL:${STARFORGE_ENABLE_VALIDATION}>,$<CONFIG:Debug>>:STARFORGE_ENABLE_VALIDATION=1>
)

starforge_set_warnings(starforge_vulkan)
