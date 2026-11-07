add_executable(starforge_sandbox
    main.cpp
)

target_link_libraries(starforge_sandbox PRIVATE starforge)

target_compile_definitions(starforge_sandbox
    PRIVATE
        STARFORGE_SHADER_DIR="${PROJECT_BINARY_DIR}/shaders"
)

starforge_set_warnings(starforge_sandbox)

if(TARGET starforge_shaders)
    add_dependencies(starforge_sandbox starforge_shaders)
endif()
