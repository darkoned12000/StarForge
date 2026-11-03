# Slang shader compilation helpers.
#
#   starforge_setup_slang()
#       Locates slangc and creates the aggregate `starforge_shaders` target.
#       Safe to call when Slang is disabled or missing; shader functions
#       then become no-ops.
#
#   starforge_add_shader(
#       SOURCE <file.slang>
#       STAGE  <compute|vertex|fragment|...>
#       ENTRY  <entry point name>
#       [PROFILE <profile>]        # default: glsl_450
#       [OUTPUT_DIR <dir>]         # default: <build>/shaders
#   )
#       Compiles one shader to <OUTPUT_DIR>/<name>.spv. A missing source file
#       produces a warning and is skipped instead of failing the build.

function(starforge_setup_slang)
    if(NOT STARFORGE_ENABLE_SLANG)
        return()
    endif()

    find_program(STARFORGE_SLANGC
        NAMES slangc
        HINTS "$ENV{HOME}/.local/opt/slang/bin"
    )

    if(STARFORGE_SLANGC)
        message(STATUS "Slang compiler: ${STARFORGE_SLANGC}")
        if(NOT TARGET starforge_shaders)
            add_custom_target(starforge_shaders ALL)
        endif()
    else()
        message(WARNING "slangc not found. Shader compilation will be skipped.")
    endif()
endfunction()

function(starforge_add_shader)
    cmake_parse_arguments(SF "" "SOURCE;STAGE;ENTRY;PROFILE;OUTPUT_DIR" "" ${ARGN})

    if(NOT STARFORGE_ENABLE_SLANG OR NOT STARFORGE_SLANGC)
        return()
    endif()

    if(NOT SF_SOURCE OR NOT SF_STAGE OR NOT SF_ENTRY)
        message(FATAL_ERROR "starforge_add_shader requires SOURCE, STAGE and ENTRY")
    endif()

    if(NOT EXISTS "${SF_SOURCE}")
        message(WARNING "Shader source not found, skipping: ${SF_SOURCE}")
        return()
    endif()

    if(NOT SF_PROFILE)
        set(SF_PROFILE glsl_450)
    endif()

    if(NOT SF_OUTPUT_DIR)
        set(SF_OUTPUT_DIR "${PROJECT_BINARY_DIR}/shaders")
    endif()

    get_filename_component(shader_name "${SF_SOURCE}" NAME_WE)
    set(shader_output "${SF_OUTPUT_DIR}/${shader_name}.spv")

    add_custom_command(
        OUTPUT "${shader_output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SF_OUTPUT_DIR}"
        COMMAND "${STARFORGE_SLANGC}"
                "${SF_SOURCE}"
                -target spirv
                -profile ${SF_PROFILE}
                -entry ${SF_ENTRY}
                -stage ${SF_STAGE}
                -o "${shader_output}"
        DEPENDS "${SF_SOURCE}"
        COMMENT "Compiling Slang shader: ${shader_name}"
        VERBATIM
    )

    add_custom_target(starforge_shader_${shader_name} DEPENDS "${shader_output}")
    add_dependencies(starforge_shaders starforge_shader_${shader_name})
endfunction()
