cmake_minimum_required(VERSION 3.26)

project(boids-vulkan)

    add_subdirectory(deps)

    add_executable(boids main.cpp)

    target_link_libraries(boids PRIVATE Vulkan::Vulkan glm glfw)
