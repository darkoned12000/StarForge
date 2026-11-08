add_executable(starforge_sandbox
    main.cpp
)

# The sandbox is the public API example: it links only the public API
# target and sees only include/starforge/ headers.
target_link_libraries(starforge_sandbox PRIVATE starforge_public_api)

starforge_set_warnings(starforge_sandbox)

if(TARGET starforge_shaders)
    add_dependencies(starforge_sandbox starforge_shaders)
endif()
