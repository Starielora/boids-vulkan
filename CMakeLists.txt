cmake_minimum_required(VERSION 3.26)

set(CMAKE_CXX_STANDARD 20)

project(boids-vulkan)

    add_subdirectory(deps)
    add_subdirectory(shaders)

    add_executable(boids
        main.cpp
        constants.hpp
        camera.hpp
        cleanup.hpp
        cleanup.cpp
        vkcheck.hpp
        setup.hpp
        setup.cpp
        boids.hpp
        boids.cpp
        light.hpp
        light.cpp
        cone.hpp
        cone.cpp
        aquarium.hpp
        aquarium.cpp
        grid.hpp
        grid.cpp
        gui.hpp
        gui.cpp
        vertex.hpp
    )

    target_link_libraries(boids PRIVATE volk glm glfw spdlog::spdlog imgui)

    add_dependencies(boids shaders)

    target_include_directories(boids PRIVATE ${CMAKE_BINARY_DIR})
    target_compile_definitions(boids PRIVATE NOMINMAX)
