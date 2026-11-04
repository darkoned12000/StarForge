add_library(starforge_math STATIC
    math.cpp
)

target_include_directories(starforge_math
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(starforge_math
    PUBLIC
        glm::glm
        starforge_core
)

target_compile_definitions(starforge_math
    PUBLIC
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLM_FORCE_RIGHT_HANDED_COORDINATE_SYSTEM
        GLM_FORCE_COLUMN_MAJOR
)

starforge_set_warnings(starforge_math)
