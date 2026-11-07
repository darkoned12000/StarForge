# Public API implementation (facades over the internal libraries).

add_library(starforge_public_api STATIC
    application.cpp
)

target_include_directories(starforge_public_api
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(starforge_public_api
    PRIVATE
        starforge_platform
)

starforge_set_warnings(starforge_public_api)
