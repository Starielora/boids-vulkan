cmake_minimum_required(VERSION 3.26)

set(CMAKE_CXX_STANDARD 20)

project(boids-vulkan)

    add_subdirectory(deps)
    add_subdirectory(shaders)

    add_executable(boids main.cpp camera.hpp)

    target_link_libraries(boids PRIVATE volk glm glfw spdlog::spdlog imgui spirv-reflect-static)

    add_dependencies(boids shaders)

    target_include_directories(boids PRIVATE ${CMAKE_BINARY_DIR})
