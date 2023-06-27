#include <iostream>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <cassert>

// TODO error message
#define VK_CHECK(f) do { const auto result = f; if(result != VK_SUCCESS) throw std::runtime_error("");} while(0)

auto create_glfw_window()
{
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    const auto window = glfwCreateWindow(800, 600, "boids", nullptr, nullptr);
    assert(window);

    return window;
}

auto create_vulkan_instance()
{
    auto instance = VkInstance{};

    auto glfw_extensions_count = uint32_t{ 0 };
    const auto* glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extensions_count);

    const auto application_info = VkApplicationInfo{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = "boids",
        .applicationVersion = VK_MAKE_VERSION(0, 0, 69),
        .pEngineName = "i'm tryna learn vk",
        .engineVersion = VK_MAKE_VERSION(0, 0, 420),
        .apiVersion = VK_API_VERSION_1_3
    };

    const auto create_info = VkInstanceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .pApplicationInfo = &application_info,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = glfw_extensions_count,
        .ppEnabledExtensionNames = glfw_extensions
    };

    VK_CHECK(vkCreateInstance(&create_info, nullptr, &instance));

    return instance;
}

int main()
{
    const auto glfw_initialized = glfwInit();
    assert(glfw_initialized == GLFW_TRUE);

    const auto window = create_glfw_window();
    const auto vk_instance = create_vulkan_instance();

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
    }

    vkDestroyInstance(vk_instance, nullptr);

    glfwDestroyWindow(window);
    glfwTerminate();
}
