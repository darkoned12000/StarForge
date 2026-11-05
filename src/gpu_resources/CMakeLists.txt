add_library(starforge_gpu STATIC
    allocator.cpp
    buffer.cpp
    compute_pipeline.cpp
    gpu_culling.cpp
    image.cpp
    staging_ring_buffer.cpp
    transfer.cpp
    ${PROJECT_SOURCE_DIR}/third_party/vma/vk_mem_alloc.cpp
)

if(MSVC)
    set_source_files_properties(${PROJECT_SOURCE_DIR}/third_party/vma/vk_mem_alloc.cpp
        PROPERTIES COMPILE_OPTIONS "/w"
    )
else()
    set_source_files_properties(${PROJECT_SOURCE_DIR}/third_party/vma/vk_mem_alloc.cpp
        PROPERTIES COMPILE_OPTIONS "-w"
    )
endif()

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
)

starforge_set_warnings(starforge_gpu)
