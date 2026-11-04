cmake_minimum_required(VERSION 3.30)

project(STARFORGE
    VERSION 0.1.0
    DESCRIPTION "Space-focused graphics API and rendering technology"
    LANGUAGES CXX
)

# --------------------------------------------------
# Language configuration
# --------------------------------------------------

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# No C++ modules yet; skips the clang-scan-deps requirement and speeds up builds.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")

# --------------------------------------------------
# Build options
# --------------------------------------------------

option(STARFORGE_ENABLE_SLANG
    "Enable Slang shader compilation"
    ON
)

option(STARFORGE_ENABLE_VALIDATION
    "Enable Vulkan validation in Debug builds"
    ON
)

option(STARFORGE_BUILD_SANDBOX
    "Build the sandbox application"
    ON
)

option(STARFORGE_BUILD_TESTS
    "Build unit tests"
    ON
)

# --------------------------------------------------
# Helper modules
# --------------------------------------------------

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

include(StarforgeWarnings)
include(StarforgeShaders)

# --------------------------------------------------
# Dependencies
# --------------------------------------------------

find_package(Vulkan REQUIRED)
find_package(SDL3 CONFIG REQUIRED)

# GLM (math library) — FetchContent fallback if not installed system-wide.
find_package(glm CONFIG QUIET)
if(NOT glm_FOUND)
    message(STATUS "glm not installed; fetching v1.0.1")
    include(FetchContent)
    FetchContent_Declare(glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        1.0.1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(glm)
endif()

# --------------------------------------------------
# Libraries
# --------------------------------------------------

add_subdirectory(src)

# --------------------------------------------------
# Shaders
# --------------------------------------------------

starforge_setup_slang()

starforge_add_shader(
    SOURCE "${PROJECT_SOURCE_DIR}/shaders/compute/test_compute.slang"
    STAGE  compute
    ENTRY  computeMain
)

starforge_add_shader(
    SOURCE "${PROJECT_SOURCE_DIR}/shaders/graphics/test_triangle.slang"
    STAGE  vertex
    ENTRY  vertexMain
    OUTPUT_NAME test_triangle.vert
)

starforge_add_shader(
    SOURCE "${PROJECT_SOURCE_DIR}/shaders/graphics/test_triangle.slang"
    STAGE  fragment
    ENTRY  fragmentMain
    OUTPUT_NAME test_triangle.frag
)

starforge_add_shader(
    SOURCE "${PROJECT_SOURCE_DIR}/shaders/graphics/test_cube.slang"
    STAGE  vertex
    ENTRY  vertexMain
    OUTPUT_NAME test_cube.vert
)

starforge_add_shader(
    SOURCE "${PROJECT_SOURCE_DIR}/shaders/graphics/test_cube.slang"
    STAGE  fragment
    ENTRY  fragmentMain
    OUTPUT_NAME test_cube.frag
)

starforge_add_shader(
    SOURCE "${PROJECT_SOURCE_DIR}/shaders/graphics/test_depth.slang"
    STAGE  vertex
    ENTRY  vertexMain
    OUTPUT_NAME test_depth.vert
)

starforge_add_shader(
    SOURCE "${PROJECT_SOURCE_DIR}/shaders/graphics/test_depth.slang"
    STAGE  fragment
    ENTRY  fragmentMain
    OUTPUT_NAME test_depth.frag
)

# (test_bda.slang removed - BDA debugging complete)

# --------------------------------------------------
# Applications and tests
# --------------------------------------------------

if(STARFORGE_BUILD_SANDBOX)
    add_subdirectory(apps/sandbox)
endif()

if(STARFORGE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# --------------------------------------------------
# Project information
# --------------------------------------------------

message(STATUS "------------------------------------")
message(STATUS " STARFORGE ${PROJECT_VERSION}")
message(STATUS "------------------------------------")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Vulkan: ${Vulkan_VERSION}")
message(STATUS "SDL3: ${SDL3_DIR}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Validation (Debug only): ${STARFORGE_ENABLE_VALIDATION}")
message(STATUS "Sandbox: ${STARFORGE_BUILD_SANDBOX}  Tests: ${STARFORGE_BUILD_TESTS}")
message(STATUS "------------------------------------")
