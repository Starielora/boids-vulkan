cmake_minimum_required(VERSION 3.26)

macro(append_shader_path variable)
    string(APPEND ${variable} "\n\t")
    string(APPEND ${variable} "constexpr std::string_view ${SHADER_VARIABLE_NAME_HEADER}(\"${CMAKE_CURRENT_BINARY_DIR}/${SHADER_OUTPUT_FILE}\");")
endmacro()

macro(add_shader)
    set(options VERTEX FRAGMENT)
    set(oneValueArgs INPUT_FILE OUTPUT_FILE VARIABLE_NAME_HEADER)
    cmake_parse_arguments(SHADER "${options}" "${oneValueArgs}" "" ${ARGN})

    add_custom_command(
        OUTPUT ${SHADER_OUTPUT_FILE}
        COMMAND Vulkan::glslc ${CMAKE_CURRENT_LIST_DIR}/${SHADER_INPUT_FILE} -o ${SHADER_OUTPUT_FILE}
        DEPENDS ${SHADER_INPUT_FILE}
        VERBATIM
    )

    if(SHADER_VERTEX)
        append_shader_path(vertex_shaders)
    elseif(SHADER_FRAGMENT)
        append_shader_path(fragment_shaders)
    else()
        message(FATAL_ERROR "Unknown shader type: ${SHADER_TYPE}")
    endif()

    list(APPEND target_depends ${SHADER_OUTPUT_FILE})
    list(APPEND target_sources ${SHADER_INPUT_FILE})
endmacro()

project(shaders)

    set(vertex_shaders "")
    set(fragment_shaders "")
    set(target_depends "")
    set(target_sources "")

    add_shader(VERTEX
        INPUT_FILE triangle.vert
        OUTPUT_FILE triangle.vert.spv
        VARIABLE_NAME_HEADER triangle
    )

    add_shader(FRAGMENT
        INPUT_FILE triangle.frag
        OUTPUT_FILE triangle.frag.spv
        VARIABLE_NAME_HEADER triangle
    )

    add_shader(VERTEX
        INPUT_FILE grid.vert
        OUTPUT_FILE grid.vert.spv
        VARIABLE_NAME_HEADER grid
    )

    add_shader(FRAGMENT
        INPUT_FILE grid.frag
        OUTPUT_FILE grid.frag.spv
        VARIABLE_NAME_HEADER grid
    )

    add_shader(VERTEX
        INPUT_FILE aquarium.vert
        OUTPUT_FILE aquarium.vert.spv
        VARIABLE_NAME_HEADER aquarium
    )

    add_shader(FRAGMENT
        INPUT_FILE aquarium.frag
        OUTPUT_FILE aquarium.frag.spv
        VARIABLE_NAME_HEADER aquarium
    )

    add_shader(VERTEX
        INPUT_FILE debug_light_cube.vert
        OUTPUT_FILE debug_light_cube.vert.spv
        VARIABLE_NAME_HEADER debug_light_cube
    )

    add_shader(FRAGMENT
        INPUT_FILE debug_light_cube.frag
        OUTPUT_FILE debug_light_cube.frag.spv
        VARIABLE_NAME_HEADER debug_light_cube
    )

    add_shader(VERTEX
        INPUT_FILE debug_spatial_hash_cube.vert
        OUTPUT_FILE debug_spatial_hash_cube.vert.spv
        VARIABLE_NAME_HEADER debug_spatial_hash_cube
    )

    add_shader(FRAGMENT
        INPUT_FILE debug_spatial_hash_cube.frag
        OUTPUT_FILE debug_spatial_hash_cube.frag.spv
        VARIABLE_NAME_HEADER debug_spatial_hash_cube
    )

    set(shaders_header_contents
"#pragma once

#include <string_view>

constexpr auto shader_entry_point = std::string_view(\"main\");

namespace shader_path::vertex {${vertex_shaders}
}

namespace shader_path::fragment {${fragment_shaders}
}
")
    file(GENERATE OUTPUT shaders.h CONTENT "${shaders_header_contents}")

    list(APPEND target_sources shaders.h)

    add_custom_target(shaders
        ALL
        DEPENDS ${target_depends}
        SOURCES ${target_sources}
    )
