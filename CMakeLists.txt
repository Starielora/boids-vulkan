cmake_minimum_required(VERSION 3.26)

set(CMAKE_CXX_STANDARD 20)

project(boids-vulkan)

    add_subdirectory(deps)
    add_subdirectory(shaders)

    add_executable(boids
        src/main.cpp
        src/constants.hpp
        src/camera.hpp
        src/cleanup.hpp
        src/cleanup.cpp
        src/vkcheck.hpp
        src/setup.hpp
        src/setup.cpp
        src/boids.hpp
        src/boids.cpp
        src/light.hpp
        src/light.cpp
        src/cone.hpp
        src/cone.cpp
        src/aquarium.hpp
        src/aquarium.cpp
        src/grid.hpp
        src/grid.cpp
        src/gui.hpp
        src/gui.cpp
        src/vertex.hpp
        src/shader_module_cache.hpp
        src/shader_module_cache.cpp
        src/boids_update.hpp
        src/boids_update.cpp
    )

    target_link_libraries(boids PRIVATE volk glm glfw spdlog::spdlog imgui)

    add_dependencies(boids shaders)

    target_include_directories(boids PRIVATE ${CMAKE_BINARY_DIR})
    target_compile_definitions(boids PRIVATE NOMINMAX)
