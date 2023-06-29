#define NOMINMAX
#include <iostream>

#define VK_NO_PROTOTYPES
#include <Volk/volk.h>
#include <GLFW/glfw3.h>

#include <spdlog/spdlog.h>

#include <cassert>
#include <vector>
#include <limits>

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

VkBool32 debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
    const auto type = [messageTypes]()
    {
        auto type = std::string{};
        if (messageTypes & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT)
        {
            type += "[General]";
        }

        if (messageTypes & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
        {
            type += "[Performance]";
        }

        if (messageTypes & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
        {
            type += "[Validation]";
        }

        return type;
    }();

    // TODO improve formatting
    const auto message = fmt::format("{} ({}): {}", type, pCallbackData->pMessageIdName, pCallbackData->pMessage);

    // TODO this doesn't look too good
    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
    {
        spdlog::info(message);
    }

    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        spdlog::error(message);
    }

    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        spdlog::warn(message);
    }

    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
    {
        spdlog::trace(message);
    }

    return VK_FALSE;
}

static const auto debug_utils_messenger_create_info = VkDebugUtilsMessengerCreateInfoEXT{
    .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
    .pNext = nullptr,
    .flags = 0,
    .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
    .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
    .pfnUserCallback = &debug_callback,
    .pUserData = nullptr
};

auto create_vulkan_instance(const std::vector<const char*>& layers, const std::vector<const char*>& extensions)
{
    assert(layers.size() < std::numeric_limits<uint32_t>::max());
    assert(extensions.size() < std::numeric_limits<uint32_t>::max());

    auto instance = VkInstance{};

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
        .pNext = &debug_utils_messenger_create_info,
        .flags = 0,
        .pApplicationInfo = &application_info,
        .enabledLayerCount = static_cast<uint32_t>(layers.size()),
        .ppEnabledLayerNames = layers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data()
    };

    VK_CHECK(vkCreateInstance(&create_info, nullptr, &instance));

    return instance;
}

auto check_instance_layers(const std::vector<const char*>& requested_layers)
{
    auto instance_layer_propertes_count = uint32_t{ 0 };
    VK_CHECK(vkEnumerateInstanceLayerProperties(&instance_layer_propertes_count, nullptr));
    auto instance_layer_properties = std::vector<VkLayerProperties>(instance_layer_propertes_count);
    VK_CHECK(vkEnumerateInstanceLayerProperties(&instance_layer_propertes_count, instance_layer_properties.data()));

    // TODO check all layers
    bool found = false;
    for (const auto* requested : requested_layers)
    {
        for (const auto& instance_layer : instance_layer_properties)
        {
            found = found || (std::strcmp(requested, instance_layer.layerName) == 0);
        }

        if (!found)
        {
            std::cerr << requested << " layer not found.\n";
        }
    }

    return found;
}

int main()
{
    spdlog::info("Start");
    VK_CHECK(volkInitialize());

    const auto glfw_initialized = glfwInit();
    assert(glfw_initialized == GLFW_TRUE);

    auto glfw_extensions_count = uint32_t{ 0 };
    const auto** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extensions_count);

    const auto window = create_glfw_window();

    const auto requested_instance_layers = std::vector<const char*>{ "VK_LAYER_KHRONOS_validation" };
    auto requested_extensions = std::vector<const char*>(glfw_extensions, glfw_extensions + glfw_extensions_count);
    requested_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    const auto instance_layers_found = check_instance_layers(requested_instance_layers);
    if (!instance_layers_found)
    {
        return 1;
    }

    const auto vk_instance = create_vulkan_instance(requested_instance_layers, requested_extensions);
    volkLoadInstance(vk_instance);

    auto debug_messenger = VkDebugUtilsMessengerEXT{ 0 };
    VK_CHECK(vkCreateDebugUtilsMessengerEXT(vk_instance, &debug_utils_messenger_create_info, nullptr, &debug_messenger));

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
    }

    vkDestroyDebugUtilsMessengerEXT(vk_instance, debug_messenger, nullptr);
    vkDestroyInstance(vk_instance, nullptr);

    glfwDestroyWindow(window);
    glfwTerminate();
}
