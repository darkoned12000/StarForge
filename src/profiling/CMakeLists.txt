add_library(starforge_profiling STATIC
    profile.cpp
)

target_include_directories(starforge_profiling
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_compile_definitions(starforge_profiling
    PUBLIC
        $<$<BOOL:${STARFORGE_ENABLE_PROFILING}>:STARFORGE_ENABLE_PROFILING=1>
        $<$<BOOL:${STARFORGE_ENABLE_TRACY}>:STARFORGE_ENABLE_TRACY=1>
        $<$<BOOL:${STARFORGE_ENABLE_TRACY}>:TRACY_ENABLE>
)

if(STARFORGE_ENABLE_TRACY)
    target_link_libraries(starforge_profiling PUBLIC TracyClient)
endif()

starforge_set_warnings(starforge_profiling)
