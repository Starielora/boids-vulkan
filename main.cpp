#include <iostream>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <cassert>

int main()
{
    const auto glfwInitialized = glfwInit();
    assert(glfwInitialized == GLFW_TRUE);

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    const auto window = glfwCreateWindow(800, 600, "boids", nullptr, nullptr);
    assert(window);

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
