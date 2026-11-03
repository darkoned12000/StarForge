add_library(starforge_platform STATIC
    window.cpp
)

target_include_directories(starforge_platform PUBLIC ${PROJECT_SOURCE_DIR}/src)

target_link_libraries(starforge_platform
    PUBLIC  starforge_core Vulkan::Vulkan
    PRIVATE SDL3::SDL3
)

starforge_set_warnings(starforge_platform)
