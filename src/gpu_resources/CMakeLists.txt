add_library(starforge_gpu STATIC
    allocator.cpp
    buffer.cpp
    compute_pipeline.cpp
    image.cpp
    transfer.cpp
    ${PROJECT_SOURCE_DIR}/third_party/vma/vk_mem_alloc.cpp
)

set_source_files_properties(${PROJECT_SOURCE_DIR}/third_party/vma/vk_mem_alloc.cpp
    PROPERTIES COMPILE_OPTIONS "-w"
)

target_include_directories(starforge_gpu
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_include_directories(starforge_gpu SYSTEM
    PUBLIC
        ${PROJECT_SOURCE_DIR}/third_party/vma
)

target_link_libraries(starforge_gpu
    PUBLIC
        starforge_vulkan
        Vulkan::Vulkan
)

starforge_set_warnings(starforge_gpu)

# Suppress Clang nullability warnings from VMA's header (third-party code).
# These must come after starforge_set_warnings to override -Wall/-Wextra.
target_compile_options(starforge_gpu PRIVATE
    -Wno-nullability-extension
    -Wno-nullability-completeness
    -Wno-unused-private-field
)
