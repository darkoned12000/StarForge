add_library(starforge_asset STATIC
    gltf_loader.cpp
    asset_manager.cpp
    file_watcher.cpp
    hot_reload.cpp
    material_mapping.cpp
    mesh_optimizer.cpp
    lod_chain.cpp
)

target_link_libraries(starforge_asset
    PUBLIC
        meshoptimizer
)

target_include_directories(starforge_asset
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
    PRIVATE
        ${PROJECT_SOURCE_DIR}/third_party
        ${PROJECT_SOURCE_DIR}/third_party/stb
)

starforge_set_warnings(starforge_asset)
