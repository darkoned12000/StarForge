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

option(STARFORGE_BUILD_GAME
    "Build the First Flight game application"
    ON
)

option(STARFORGE_BUILD_TESTS
    "Build unit tests"
    ON
)

option(STARFORGE_USE_FLECS
    "Fetch and build Flecs for the ECS scene layer (M10)"
    ON
)

option(STARFORGE_USE_JOLT
    "Fetch and build Jolt Physics for the physics layer (M11)"
    ON
)

option(STARFORGE_USE_MINIAUDIO
    "Fetch and build miniaudio for the audio layer (M12)"
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

# Flecs (ECS) — FetchContent, pinned like doctest.
if(STARFORGE_USE_FLECS)
    include(FetchContent)
    FetchContent_Declare(flecs
        GIT_REPOSITORY https://github.com/SanderMertens/flecs.git
        GIT_TAG v4.2.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(flecs)
endif()

# Jolt Physics (pinned) — FetchContent, like Flecs/doctest. Jolt's
# CMake entry point lives in its Build/ subdirectory.
if(STARFORGE_USE_JOLT)
    include(FetchContent)
    set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
    set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
    # STARFORGE does not enable link-time optimization; Jolt would otherwise
    # warn that IPO has no effect for its target.
    set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
    set(USE_ASSERTS ON CACHE BOOL "" FORCE)
    set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
    set(DOUBLE_PRECISION OFF CACHE BOOL "" FORCE)
    set(CROSS_PLATFORM_DETERMINISTIC ON CACHE BOOL "" FORCE)
    # We do not use Jolt's hair/compute shaders (dxc is unavailable).
    set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
    set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE)
    set(JPH_USE_MTL OFF CACHE BOOL "" FORCE)
    set(JPH_USE_CPU_COMPUTE OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(JoltPhysics
        GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
        GIT_TAG v5.6.0
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR Build
    )
    FetchContent_MakeAvailable(JoltPhysics)
endif()

# miniaudio (audio) — FetchContent, pinned like Flecs/Jolt. The upstream
# CMakeLists provides a `miniaudio` target; examples/tests/tools stay off.
if(STARFORGE_USE_MINIAUDIO)
    include(FetchContent)
    set(MINIAUDIO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(MINIAUDIO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(MINIAUDIO_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(MINIAUDIO_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(miniaudio
        GIT_REPOSITORY https://github.com/mackron/miniaudio.git
        GIT_TAG 0.11.26
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(miniaudio)
endif()

# GLM (math library) — FetchContent fallback if not installed system-wide.
find_package(glm CONFIG QUIET)
if(NOT glm_FOUND)
    message(STATUS "glm not found system-wide; using vendored copy in third_party/glm/")
    add_library(glm INTERFACE)
    target_include_directories(glm SYSTEM INTERFACE "${PROJECT_SOURCE_DIR}/third_party/glm")
    add_library(glm::glm ALIAS glm)
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
    SOURCE "${PROJECT_SOURCE_DIR}/shaders/compute/timestamp_test.slang"
    STAGE  compute
    ENTRY  computeMain
    OUTPUT_NAME timestamp_test
)

starforge_add_shader(
    SOURCE "${PROJECT_SOURCE_DIR}/shaders/compute/cull.slang"
    STAGE  compute
    ENTRY  computeMain
    OUTPUT_NAME cull
)

starforge_add_shader(
    SOURCE "${PROJECT_SOURCE_DIR}/shaders/graphics/gpu_driven.vert.slang"
    STAGE  vertex
    ENTRY  vertexMain
    OUTPUT_NAME gpu_driven.vert
)

starforge_add_shader(
    SOURCE "${PROJECT_SOURCE_DIR}/shaders/graphics/gpu_driven.frag.slang"
    STAGE  fragment
    ENTRY  fragmentMain
    OUTPUT_NAME gpu_driven.frag
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

starforge_add_shader(
    SOURCE "${PROJECT_SOURCE_DIR}/shaders/graphics/toon.slang"
    STAGE  vertex
    ENTRY  vertexMain
    OUTPUT_NAME toon.vert
)

starforge_add_shader(
    SOURCE "${PROJECT_SOURCE_DIR}/shaders/graphics/toon.slang"
    STAGE  fragment
    ENTRY  fragmentMain
    OUTPUT_NAME toon.frag
)

# Environment shaders for space effects

starforge_add_shader(
    SOURCE "${PROJECT_SOURCE_DIR}/shaders/graphics/atmosphere.slang"
    STAGE  vertex
    ENTRY  vertexMain
    OUTPUT_NAME atmosphere.vert
)

starforge_add_shader(
    SOURCE "${PROJECT_SOURCE_DIR}/shaders/graphics/atmosphere.slang"
    STAGE  fragment
    ENTRY  fragmentMain
    OUTPUT_NAME atmosphere.frag
)

starforge_add_shader(
    SOURCE "${PROJECT_SOURCE_DIR}/shaders/graphics/planet_patch.slang"
    STAGE  vertex
    ENTRY  vertexMain
    OUTPUT_NAME planet_patch.vert
)

starforge_add_shader(
    SOURCE "${PROJECT_SOURCE_DIR}/shaders/graphics/planet_patch.slang"
    STAGE  fragment
    ENTRY  fragmentMain
    OUTPUT_NAME planet_patch.frag
)

starforge_add_shader(
    SOURCE "${PROJECT_SOURCE_DIR}/shaders/graphics/starfield.slang"
    STAGE  vertex
    ENTRY  vertexMain
    OUTPUT_NAME starfield.vert
)

starforge_add_shader(
    SOURCE "${PROJECT_SOURCE_DIR}/shaders/graphics/starfield.slang"
    STAGE  fragment
    ENTRY  fragmentMain
    OUTPUT_NAME starfield.frag
)

foreach(effect_shaders IN ITEMS nebula dust sun_distortion wormhole ftl storm billboard asteroid)
    starforge_add_shader(
        SOURCE "${PROJECT_SOURCE_DIR}/shaders/graphics/${effect_shaders}.slang"
        STAGE  vertex
        ENTRY  vertexMain
        OUTPUT_NAME ${effect_shaders}.vert
    )
    starforge_add_shader(
        SOURCE "${PROJECT_SOURCE_DIR}/shaders/graphics/${effect_shaders}.slang"
        STAGE  fragment
        ENTRY  fragmentMain
        OUTPUT_NAME ${effect_shaders}.frag
    )
endforeach()

# (test_bda.slang removed - BDA debugging complete)

# --------------------------------------------------
# Applications and tests
# --------------------------------------------------

if(STARFORGE_BUILD_SANDBOX)
    add_subdirectory(apps/sandbox)
endif()

if(STARFORGE_BUILD_GAME)
    add_subdirectory(apps/game)
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
