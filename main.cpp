#define NOMINMAX
#include <iostream>

#define VK_NO_PROTOTYPES
#include <Volk/volk.h>
#include <GLFW/glfw3.h>

#include <spdlog/spdlog.h>

#include <cassert>
#include <vector>
#include <limits>
#include <optional>
#include <set>
#include <string_view>

// TODO error message
#define VK_CHECK(f) do { const auto result = f; if(result != VK_SUCCESS) throw std::runtime_error("");} while(0)

auto create_glfw_window()
{
    spdlog::trace("Create glfw window.");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    const auto window = glfwCreateWindow(800, 600, "boids", nullptr, nullptr);
    assert(window);

    return window;
}

VkBool32 debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
    // TODO is there any message which has several types?
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
    spdlog::trace("Create vulkan instance.");
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
    spdlog::trace("Check vulkan instance layers.");
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

std::optional<uint32_t> pick_family_index(VkQueueFlagBits bits, const std::vector<VkQueueFamilyProperties>& queue_props)
{
    for (int i = 0; const auto& prop : queue_props)
    {
        const bool supports_requested_operations = prop.queueFlags & bits;

        const bool supports_graphics = prop.queueFlags & VK_QUEUE_GRAPHICS_BIT;
        const bool supports_compute = prop.queueFlags & VK_QUEUE_COMPUTE_BIT;
        const bool supports_transfer = prop.queueFlags & VK_QUEUE_TRANSFER_BIT;

        spdlog::debug("Queue family {}; count: {} | GRAPHICS {:^7} | COMPUTE {:^7} | TRANSFER {:^7} | ", i, prop.queueCount, supports_graphics, supports_compute, supports_transfer);

        if (supports_requested_operations)
        {
            spdlog::debug("Queue family supports requested operations.");
            return i;
        }
        else
        {
            spdlog::debug("Queue family does not support requested operations");
        }

        i++;
    }

    return std::nullopt;
}

bool check_device_extensions(VkPhysicalDevice device, const std::vector<const char*> required_device_extensions)
{
    auto count = uint32_t{ 0 };
    VK_CHECK(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr));
    auto props = std::vector<VkExtensionProperties>(count);
    VK_CHECK(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, props.data()));
    assert(count == props.size());

    auto extensions_set = std::set<std::string_view>(required_device_extensions.begin(), required_device_extensions.end());

    for (const auto& prop : props)
    {
        extensions_set.erase(prop.extensionName);
    }

    return extensions_set.empty();
}

auto pick_physical_device(VkInstance instance, VkSurfaceKHR surface, const std::vector<const char*> required_device_extensions)
{
    spdlog::trace("Picking physical device.");

    auto count = uint32_t{ 0 };
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, nullptr));
    auto physical_devices = std::vector<VkPhysicalDevice>(count);
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, physical_devices.data()));
    assert(count == physical_devices.size());

    spdlog::warn("Require device with at least 1 queue in family supporting GRAPHICS, COMPUTE and TRANSFER");
    constexpr auto required_bits = VkQueueFlagBits(VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT);

    for (const auto& physical_device : physical_devices)
    {
        auto props = VkPhysicalDeviceProperties{};
        vkGetPhysicalDeviceProperties(physical_device, &props);
        spdlog::info("Checking {}", props.deviceName);

        auto count = uint32_t{ 0 };
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
        auto queue_family_props = std::vector<VkQueueFamilyProperties>(count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, queue_family_props.data());
        assert(queue_family_props.size() == count);

        // TODO this condition may be a bit too restrictive, but is sufficient for development now
        const auto suitable_queue_family_index = pick_family_index(required_bits, queue_family_props);

        const auto extensions_supported = check_device_extensions(physical_device, required_device_extensions);

        if (suitable_queue_family_index.has_value() && extensions_supported)
        {
            auto is_presentation_supported = VkBool32{ false };
            vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, suitable_queue_family_index.value(), surface, &is_presentation_supported);

            if (is_presentation_supported)
            {
                spdlog::debug("Queue family {} supports presentation to surface.", suitable_queue_family_index.value());
                spdlog::info("Picking {} physical device.", props.deviceName);
                return std::tuple{physical_device, suitable_queue_family_index.value()};
            }
        }
    }

    throw std::runtime_error("No suitable physical device found. Revisit device suitability logic");
}

auto create_logical_device(VkPhysicalDevice physical_device, uint32_t queue_family_index, const std::vector<const char*>& device_extensions)
{
    const auto queue_prio = 1.f;
    const auto queue_create_info = VkDeviceQueueCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueFamilyIndex = queue_family_index,
        .queueCount = 1, // one queue should be sufficient for now
        .pQueuePriorities = &queue_prio
    };

    const auto create_info = VkDeviceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_create_info,
        .enabledLayerCount = 0, // deprecated + ignored
        .ppEnabledLayerNames = nullptr, // deprecated + ignored 
        .enabledExtensionCount = static_cast<uint32_t>(device_extensions.size()),
        .ppEnabledExtensionNames = device_extensions.data(),
        .pEnabledFeatures = nullptr // not required atm
    };
    auto device = VkDevice{ 0 };

    VK_CHECK(vkCreateDevice(physical_device, &create_info, nullptr, &device));

    return device;
}

VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR& surface_caps, VkExtent2D glfw_framebuffer_extent)
{
    if (surface_caps.currentExtent.width == std::numeric_limits<uint32_t>::max())
    {
        return surface_caps.currentExtent;
    }
    else
    {
        return VkExtent2D{
            .width = std::clamp(glfw_framebuffer_extent.width, surface_caps.minImageExtent.width, surface_caps.maxImageExtent.width),
            .height = std::clamp(glfw_framebuffer_extent.height, surface_caps.minImageExtent.height, surface_caps.maxImageExtent.height)
        };
    }
}

VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>& available_present_modes)
{
    for (const auto& mode : available_present_modes)
    {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            return mode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR; // reasonable fallback, because driver is required to support this
}

VkSurfaceFormatKHR choose_image_format(const std::vector<VkSurfaceFormatKHR>& available_formats)
{
    for (const auto& format : available_formats)
    {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return format;
        }
    }

    assert(available_formats.size() > 0);
    return available_formats[0]; // fallback
}

auto create_swapchain(VkDevice logical_device, VkPhysicalDevice physical_device, VkSurfaceKHR surface, uint32_t queue_family_index, VkExtent2D glfw_framebuffer_extent)
{
    // TODO move these out of the function to not repeat the calls in main loop
    auto surface_caps = VkSurfaceCapabilitiesKHR{};
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &surface_caps));

    auto surface_formats_count = uint32_t{ 0 };
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &surface_formats_count, nullptr));
    auto surface_formats = std::vector<VkSurfaceFormatKHR>(surface_formats_count);
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &surface_formats_count, surface_formats.data()));
    assert(surface_formats_count == surface_formats.size());

    auto present_modes_count = uint32_t{ 0 };
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_modes_count, nullptr));
    auto present_modes = std::vector<VkPresentModeKHR>(present_modes_count);
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_modes_count, present_modes.data()));
    assert(present_modes_count = present_modes.size());

    const auto extent = choose_extent(surface_caps, glfw_framebuffer_extent);
    const auto surface_format = choose_image_format(surface_formats);
    const auto min_image_count = surface_caps.minImageCount; // TODO should I request more images here?
    const auto present_mode = choose_present_mode(present_modes);

    auto create_info = VkSwapchainCreateInfoKHR{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .surface = surface,
        .minImageCount = min_image_count,
        .imageFormat = surface_format.format,
        .imageColorSpace = surface_format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 1,
        .pQueueFamilyIndices = &queue_family_index,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = present_mode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };

    auto swapchain = VkSwapchainKHR{ 0 };
    vkCreateSwapchainKHR(logical_device, &create_info, nullptr, &swapchain);

    return std::tuple{swapchain, surface_format};
}

auto get_swapchain_images(VkDevice logical_device, VkSwapchainKHR swapchain, VkFormat image_format)
{
    auto count = uint32_t{ 0 };
    VK_CHECK(vkGetSwapchainImagesKHR(logical_device, swapchain, &count, nullptr));
    auto images = std::vector<VkImage>(count);
    VK_CHECK(vkGetSwapchainImagesKHR(logical_device, swapchain, &count, images.data()));
    assert(count = images.size());

    auto image_views = std::vector<VkImageView>{};
    image_views.reserve(images.size());
    for (const auto& image : images)
    {
        const auto create_info = VkImageViewCreateInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = image_format,
            .components = VkComponentMapping {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY
            },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };

        auto image_view = VkImageView{ 0 };
        vkCreateImageView(logical_device, &create_info, nullptr, &image_view);

        image_views.push_back(image_view);
    }

    return std::tuple{images, image_views};
}

int main()
{
    spdlog::set_level(spdlog::level::trace);
    spdlog::info("Start");
    VK_CHECK(volkInitialize());

    spdlog::trace("Initialize glfw.");
    const auto glfw_initialized = glfwInit();
    assert(glfw_initialized == GLFW_TRUE);

    const auto window = create_glfw_window();

    const auto requested_instance_layers = std::vector<const char*>{ "VK_LAYER_KHRONOS_validation" };
    const auto required_device_extensions = std::vector<const char*>{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    const auto instance_layers_found = check_instance_layers(requested_instance_layers);
    if (!instance_layers_found)
    {
        return 1;
    }

    auto glfw_extensions_count = uint32_t{ 0 };
    const auto** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extensions_count);

    auto requested_extensions = std::vector<const char*>(glfw_extensions, glfw_extensions + glfw_extensions_count);
    requested_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    const auto vk_instance = create_vulkan_instance(requested_instance_layers, requested_extensions);
    volkLoadInstance(vk_instance);

    auto surface = VkSurfaceKHR{ 0 };
    VK_CHECK(glfwCreateWindowSurface(vk_instance, window, nullptr, &surface));

    auto debug_messenger = VkDebugUtilsMessengerEXT{ 0 };
    VK_CHECK(vkCreateDebugUtilsMessengerEXT(vk_instance, &debug_utils_messenger_create_info, nullptr, &debug_messenger));

    const auto [physical_device, queue_family_index] = pick_physical_device(vk_instance, surface, required_device_extensions);
    const auto logical_device = create_logical_device(physical_device, queue_family_index, required_device_extensions);

    auto present_queue = VkQueue{ 0 };
    vkGetDeviceQueue(logical_device, queue_family_index, 0, &present_queue); // TODO: hardcoded queue index
    assert(present_queue);

    int glfw_fb_extent_width, glfw_fb_extent_height;
    glfwGetFramebufferSize(window, &glfw_fb_extent_width, &glfw_fb_extent_height);
    const auto [swapchain, surface_format] = create_swapchain(logical_device, physical_device, surface, queue_family_index, {static_cast<uint32_t>(glfw_fb_extent_width), static_cast<uint32_t>(glfw_fb_extent_height)});
    const auto [swapchain_images, swapchain_image_views] = get_swapchain_images(logical_device, swapchain, surface_format.format);

    spdlog::trace("Entering main loop.");
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
    }

    spdlog::trace("Cleanup.");
    for (const auto iv : swapchain_image_views)
    {
        vkDestroyImageView(logical_device, iv, nullptr);
    }
    vkDestroySwapchainKHR(logical_device, swapchain, nullptr);
    vkDestroySurfaceKHR(vk_instance, surface, nullptr);
    vkDestroyDevice(logical_device, nullptr);
    vkDestroyDebugUtilsMessengerEXT(vk_instance, debug_messenger, nullptr);
    vkDestroyInstance(vk_instance, nullptr);

    glfwDestroyWindow(window);
    glfwTerminate();
}
