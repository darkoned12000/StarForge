# T1: CPU-only unit tests (always run, no GPU required).
# T2/T3: Headless GPU tests (labeled `gpu`, skip with exit code 77 when no suitable GPU exists).

find_package(doctest CONFIG QUIET)

if(NOT doctest_FOUND)
    message(STATUS "doctest not installed; fetching v2.4.11")
    include(FetchContent)
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG        v2.4.11
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(doctest)
endif()

add_executable(starforge_tests
    main.cpp
    test_version.cpp
    test_vulkan_version.cpp
    test_gpu_capabilities.cpp
    test_swapchain_selection.cpp
    test_precision_characterization.cpp
    test_math_conventions.cpp
    test_world_position.cpp
    test_camera_relative.cpp
    test_translation_invariance.cpp
    test_render_object.cpp
    test_invariance_proof.cpp
    test_statistics.cpp
    test_benchmark_result.cpp
    test_benchmark_compare.cpp
    test_benchmark_scene.cpp
    test_cpu_timer.cpp
    test_content_generation.cpp
    test_content_integration.cpp
    test_planets.cpp
    test_planets_new.cpp
    test_job_scheduler.cpp
    test_job_determinism.cpp
    test_instance_data.cpp
    test_instance_data_update.cpp
    test_public_application.cpp
    test_public_math_world.cpp
    test_public_renderer.cpp
    test_public_starfield.cpp
)

target_link_libraries(starforge_tests
    PRIVATE
        starforge_core
        starforge_bench
        starforge_content
        starforge_jobs
        starforge_planets
        starforge_math
        starforge_spatial
        starforge_stats
        starforge_vulkan
        starforge_gpu
        starforge_environment
        starforge_public_api
        doctest::doctest
)

starforge_set_warnings(starforge_tests)

add_test(NAME starforge_tests COMMAND starforge_tests)

add_executable(starforge_gpu_tests
    gpu_main.cpp
    test_headless_device.cpp
    test_gpu_resources_upload.cpp
    test_compute_dispatch.cpp
    test_clear_render.cpp
    test_image_readback.cpp
    test_depth_format.cpp
    test_offscreen_render.cpp
    test_mesh_render.cpp
    test_bda_simple.cpp
    test_cube_render.cpp
    test_toon_render.cpp
    test_gpu_cull_parity.cpp
    test_gpu_driven_benchmark.cpp
    test_depth_ordering.cpp
    test_starfield_render.cpp
    test_planet_patch_render.cpp
    test_environment_render.cpp
    test_asteroid_render.cpp
    test_environment_effects.cpp
    test_golden_starfield.cpp
    test_atmosphere_render.cpp
    test_vulkan_retirement.cpp
    test_naive_control.cpp
    test_gpu_timestamps.cpp
    test_vma_memory_snapshot.cpp
    test_benchmark_baseline.cpp
)

target_link_libraries(starforge_gpu_tests
    PRIVATE
        starforge_core
        starforge_bench
        starforge_math
        starforge_planets
        starforge_spatial
        starforge_vulkan
        starforge_gpu
        starforge_environment
        doctest::doctest
)

target_compile_definitions(starforge_gpu_tests
    PRIVATE
        STARFORGE_SHADER_DIR="${PROJECT_BINARY_DIR}/shaders"
        STARFORGE_BASELINE_DIR="${PROJECT_BINARY_DIR}/benchmarks/baselines"
        STARFORGE_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/tests/goldens"
)

target_include_directories(starforge_gpu_tests SYSTEM
    PRIVATE
        ${PROJECT_SOURCE_DIR}/third_party/stb
)

# glm::glm linked via starforge_math (PUBLIC)

target_include_directories(starforge_tests SYSTEM
    PRIVATE
        ${PROJECT_SOURCE_DIR}/third_party
)

if(TARGET starforge_shaders)
    add_dependencies(starforge_gpu_tests starforge_shaders)
endif()

starforge_set_warnings(starforge_gpu_tests)

add_test(NAME starforge_gpu_tests COMMAND starforge_gpu_tests)
set_tests_properties(starforge_gpu_tests PROPERTIES
    LABELS "gpu"
    SKIP_RETURN_CODE 77
)

if(NOT CMAKE_CROSSCOMPILING)
    add_custom_command(TARGET starforge_tests POST_BUILD
        COMMAND starforge_tests
        COMMENT "Running CPU unit tests"
    )
endif()

add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS starforge_tests starforge_gpu_tests
)

# M9.8 leakage gates: public headers must stay Vulkan/VMA/SDL/GLM-free,
# and an external consumer must compile against include/ alone.
find_program(STARFORGE_PYTHON3 NAMES python3)
if(STARFORGE_PYTHON3)
    add_test(NAME public_leakage_scan
        COMMAND ${STARFORGE_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/check_public_leakage.py
                ${PROJECT_SOURCE_DIR}/include/starforge)
    add_test(NAME public_leakage_selftest
        COMMAND ${STARFORGE_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/check_public_leakage.py --self-test)
endif()

add_test(NAME public_consumer_compile
    COMMAND ${CMAKE_CXX_COMPILER} -std=c++26 -fsyntax-only
            -I${PROJECT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/public_consumer.cpp)
