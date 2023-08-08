#include "camera.hpp"

#define NOMINMAX
#define VK_NO_PROTOTYPES
#include <Volk/volk.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

#include <spdlog/spdlog.h>
#include <spdlog/fmt/compile.h>
#include <shaders/shaders.h>

#include <cassert>
#include <vector>
#include <limits>
#include <optional>
#include <set>
#include <string_view>
#include <fstream>
#include <array>
#include <numbers>
#include <cmath>
#include <stack>
#include <span>
#include <random>

// TODO error message
#define VK_CHECK(f) do { const auto result = f; if(result != VK_SUCCESS) {spdlog::error("{}: {}", #f, result); throw std::runtime_error("");}} while(0)
constexpr bool VALIDATION_LAYERS = true;
constexpr auto depth_format = VK_FORMAT_D32_SFLOAT; // TODO query device support
constexpr auto msaa_samples = VK_SAMPLE_COUNT_8_BIT; // TODO query device
constexpr auto shader_entry_point = std::string_view("main");

camera g_camera;
bool g_gui_mode = false;
glm::vec2 gui_mode_mouse_pos{};
const auto flip_clip_space = glm::scale(glm::mat4(1.), glm::vec3(1, -1, 1));

namespace cleanup
{
    auto general_queue = std::stack<std::function<void()>>();
    auto swapchain_queue = std::stack<std::function<void()>>();

    void flush(decltype(general_queue)& queue)
    {
        while (!queue.empty())
        {
            queue.top()();
            queue.pop();
        }
    }
}

struct vertex
{
    glm::vec3 pos;
    glm::vec3 color;
};

struct cone_instance
{
    glm::vec4 position = glm::vec4(0, 0, 0, 0);
    glm::vec4 direction = glm::vec4(0, 0, 0, 0);
    glm::vec4 velocity = glm::vec4(0, 0, 0, 0);
    glm::vec4 color = glm::vec4(0, 0, 0, 1);
    glm::mat4 model_matrix = glm::mat4(1.);
};

auto read_file(const std::string_view filename)
{
    spdlog::debug("Reading file: {}", filename);
    auto file = std::ifstream(filename.data(), std::ios::binary);
    if (!file.is_open())
    {
        spdlog::error("Could not open file {}", filename);
        throw std::runtime_error("");
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

namespace window
{
    void mouse_callback(GLFWwindow* window, double x, double y)
    {
        if (!g_gui_mode)
        {
            g_camera.look_around({ x, y });
        }
    }

    void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
    {
        if (key == GLFW_KEY_F && action == GLFW_RELEASE)
        {
            // TODO this impl is buggy when pressing key while moving mouse
            g_gui_mode = !g_gui_mode;
            if (g_gui_mode)
            {
                double x, y;
                glfwGetCursorPos(window, &x, &y);
                gui_mode_mouse_pos = { x, y };
                int w, h;
                glfwGetWindowSize(window, &w, &h);
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                glfwSetCursorPos(window, w / 2, h / 2);
            }
            else
            {
                glfwSetCursorPos(window, gui_mode_mouse_pos.x, gui_mode_mouse_pos.y);
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            }

        }
    }

    auto create(decltype(cleanup::general_queue)& cleanup_queue)
    {
        spdlog::trace("Initialize glfw.");
        const auto glfw_initialized = glfwInit();
        assert(glfw_initialized == GLFW_TRUE);

        spdlog::trace("Create glfw window.");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        glfwWindowHint(GLFW_MAXIMIZED, GL_TRUE);

        const auto window = glfwCreateWindow(800, 600, "boids", nullptr, nullptr);
        assert(window);

        glfwSetCursorPosCallback(window, mouse_callback);
        glfwSetKeyCallback(window, key_callback);
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

        cleanup_queue.push([window]() {
            glfwDestroyWindow(window);
            glfwTerminate();
        });

        return window;
    }

    auto get_vk_extensions()
    {
        auto glfw_extensions_count = uint32_t{ 0 };
        const auto** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extensions_count);
        return std::vector<const char*>(glfw_extensions, glfw_extensions + glfw_extensions_count);
    }

    auto create_vk_surface(VkInstance vk_instance, GLFWwindow* window, decltype(cleanup::general_queue)& cleanup_queue)
    {
        auto surface = VkSurfaceKHR{ 0 };
        VK_CHECK(glfwCreateWindowSurface(vk_instance, window, nullptr, &surface));

        cleanup_queue.push([vk_instance, surface]() { vkDestroySurfaceKHR(vk_instance, surface, nullptr); });

        return surface;
    }

    auto get_extent(GLFWwindow* window)
    {
        int glfw_fb_extent_width, glfw_fb_extent_height;
        glfwGetFramebufferSize(window, &glfw_fb_extent_width, &glfw_fb_extent_height);
        return VkExtent2D{ static_cast<uint32_t>(glfw_fb_extent_width), static_cast<uint32_t>(glfw_fb_extent_height) };
    }
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

constexpr auto debug_utils_messenger_create_info = VkDebugUtilsMessengerCreateInfoEXT{
    .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
    .pNext = nullptr,
    .flags = 0,
    .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
    .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
    .pfnUserCallback = &debug_callback,
    .pUserData = nullptr
};

auto create_debug_utils_messenger(VkInstance vk_instance, decltype(cleanup::general_queue)& cleanup_queue)
{
    auto debug_messenger = VkDebugUtilsMessengerEXT{ 0 };
    VK_CHECK(vkCreateDebugUtilsMessengerEXT(vk_instance, &debug_utils_messenger_create_info, nullptr, &debug_messenger));

    cleanup_queue.push([vk_instance, debug_messenger]() { vkDestroyDebugUtilsMessengerEXT(vk_instance, debug_messenger, nullptr); });
}

auto create_vulkan_instance(const std::vector<const char*>& layers, const std::vector<const char*>& extensions, decltype(cleanup::general_queue)& cleanup_queue)
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

    cleanup_queue.push([instance]() { vkDestroyInstance(instance, nullptr); });

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
            spdlog::error("Requested layer not found: {}", requested);
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
                return std::tuple{physical_device, suitable_queue_family_index.value(), props};
            }
        }
    }

    throw std::runtime_error("No suitable physical device found. Revisit device suitability logic");
}

auto create_logical_device(VkPhysicalDevice physical_device, uint32_t queue_family_index, const std::vector<const char*>& device_extensions, decltype(cleanup::general_queue)& cleanup_queue)
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

    auto features = VkPhysicalDeviceFeatures{};
    features.fillModeNonSolid = VK_TRUE;
    features.wideLines = VK_TRUE;

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
        .pEnabledFeatures = &features
    };

    auto device = VkDevice{ 0 };
    VK_CHECK(vkCreateDevice(physical_device, &create_info, nullptr, &device));

    cleanup_queue.push([device]() { vkDestroyDevice(device, nullptr); });

    auto present_queue = VkQueue{ 0 };
    vkGetDeviceQueue(device, queue_family_index, 0, &present_queue); // TODO: hardcoded queue index
    assert(present_queue);

    return std::tuple{ device, present_queue };
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
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return format;
        }
    }

    assert(available_formats.size() > 0);
    return available_formats[0]; // fallback
}

auto create_swapchain(VkDevice logical_device, VkPhysicalDevice physical_device, VkSurfaceKHR surface, uint32_t queue_family_index, VkExtent2D glfw_framebuffer_extent, decltype(cleanup::general_queue)& cleanup_queue)
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

    cleanup_queue.push([logical_device, swapchain]() { vkDestroySwapchainKHR(logical_device, swapchain, nullptr); });

    return std::tuple{swapchain, surface_format};
}

auto create_color_image_view(VkDevice logical_device, VkFormat format, VkImage image, decltype(cleanup::general_queue)& cleanup_queue)
{
    const auto create_info = VkImageViewCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .components = VkComponentMapping{
            .r = VK_COMPONENT_SWIZZLE_IDENTITY,
            .g = VK_COMPONENT_SWIZZLE_IDENTITY,
            .b = VK_COMPONENT_SWIZZLE_IDENTITY,
            .a = VK_COMPONENT_SWIZZLE_IDENTITY,
        },
        .subresourceRange = VkImageSubresourceRange{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    auto view = VkImageView{};
    VK_CHECK(vkCreateImageView(logical_device, &create_info, nullptr, &view));

    cleanup_queue.push([logical_device, view]() { vkDestroyImageView(logical_device, view, nullptr); });

    return view;
}

auto get_swapchain_images(VkDevice logical_device, VkSwapchainKHR swapchain, VkFormat image_format, decltype(cleanup::general_queue)& cleanup_queue)
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
        const auto view = create_color_image_view(logical_device, image_format, image, cleanup_queue);
        image_views.push_back(view);
    }

    return std::tuple{images, image_views};
}

VkShaderModule create_shader_module(VkDevice logical_device, const std::vector<uint8_t>& code, decltype(cleanup::general_queue)& cleanup_queue)
{
    const auto create_info = VkShaderModuleCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .codeSize = code.size(),
        .pCode = reinterpret_cast<const uint32_t*>(code.data())
    };

    auto shader_module = VkShaderModule{};
    VK_CHECK(vkCreateShaderModule(logical_device, &create_info, nullptr, &shader_module));

    cleanup_queue.push([logical_device, shader_module]() { vkDestroyShaderModule(logical_device, shader_module, nullptr); });

    return shader_module;
}

auto create_render_pass(VkDevice logical_device, VkFormat swapchain_format, VkFormat depth_format, VkSampleCountFlagBits samples, decltype(cleanup::general_queue)& cleanup_queue)
{
    const auto color_attachment = VkAttachmentDescription{
        .flags = 0,
        .format = swapchain_format,
        .samples = samples,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    const auto depth_attachment = VkAttachmentDescription{
        .flags = 0,
        .format = depth_format,
        .samples = samples,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    };

    const auto color_resolve_attachment = VkAttachmentDescription{
        .flags = 0,
        .format = swapchain_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };

    const auto color_attachment_reference = VkAttachmentReference{
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };

    const auto depth_attachment_reference = VkAttachmentReference{
        .attachment = 1,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    };

    const auto color_resolve_reference = VkAttachmentReference{
        .attachment = 2,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };

    const auto subpass = VkSubpassDescription{
        .flags = 0,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .inputAttachmentCount = 0,
        .pInputAttachments = nullptr,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment_reference,
        .pResolveAttachments = &color_resolve_reference,
        .pDepthStencilAttachment = &depth_attachment_reference,
        .preserveAttachmentCount = 0,
        .pPreserveAttachments = nullptr
    };

    const auto subpass_dependencies = std::array{
        VkSubpassDependency{
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dependencyFlags = 0
        },
        VkSubpassDependency{
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dependencyFlags = 0
        },
    };

    const auto attachments = std::array{ color_attachment, depth_attachment, color_resolve_attachment };
    const auto render_pass_create_info = VkRenderPassCreateInfo{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .attachmentCount = attachments.size(),
        .pAttachments = attachments.data(),
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = subpass_dependencies.size(),
        .pDependencies = subpass_dependencies.data()
    };

    auto render_pass = VkRenderPass{};
    VK_CHECK(vkCreateRenderPass(logical_device, &render_pass_create_info, nullptr, &render_pass));

    cleanup_queue.push([logical_device, render_pass]() { vkDestroyRenderPass(logical_device, render_pass, nullptr); });

    return render_pass;
}

auto create_pipeline_layout(VkDevice logical_device, const std::vector<VkDescriptorSetLayout>& set_layouts, decltype(cleanup::general_queue)& cleanup_queue)
{
    const auto push_constant_range = VkPushConstantRange{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(float)
    };

    const auto pipeline_layout_create_info = VkPipelineLayoutCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .setLayoutCount = static_cast<uint32_t>(set_layouts.size()),
        .pSetLayouts = set_layouts.data(),
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant_range
    };

    auto pipeline_layout = VkPipelineLayout{};
    VK_CHECK(vkCreatePipelineLayout(logical_device, &pipeline_layout_create_info, nullptr, &pipeline_layout));

    cleanup_queue.push([logical_device, pipeline_layout]() { vkDestroyPipelineLayout(logical_device, pipeline_layout, nullptr); });

    return pipeline_layout;
}

auto create_graphics_pipelines(VkDevice logical_device, const std::vector<VkGraphicsPipelineCreateInfo>& create_infos, decltype(cleanup::general_queue)& cleanup_queue)
{
    auto pipelines = std::vector<VkPipeline>(create_infos.size());
    vkCreateGraphicsPipelines(logical_device, VK_NULL_HANDLE, create_infos.size(), create_infos.data(), nullptr, pipelines.data());

    cleanup_queue.push([logical_device, pipelines]() {
        for (const auto pipeline : pipelines)
        {
            vkDestroyPipeline(logical_device, pipeline, nullptr);
        }
    });
    return pipelines;
}

auto create_swapchain_framebuffers(VkDevice logical_device, VkRenderPass render_pass, const std::vector<VkImageView>& color_imageviews, const std::vector<VkImageView>& swapchain_imageviews, const std::vector<VkImageView> depth_image_views, VkExtent2D swapchain_extent, decltype(cleanup::general_queue)& cleanup_queue)
{
    assert(swapchain_imageviews.size() == depth_image_views.size());

    auto framebuffers = std::vector<VkFramebuffer>(swapchain_imageviews.size());

    for (std::size_t i = 0; i < swapchain_imageviews.size(); ++i)
    {
        const auto attachments = std::array{ color_imageviews[i], depth_image_views[i], swapchain_imageviews[i] };
        const auto create_info = VkFramebufferCreateInfo{
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .renderPass = render_pass,
            .attachmentCount = attachments.size(),
            .pAttachments = attachments.data(),
            .width = swapchain_extent.width,
            .height = swapchain_extent.height,
            .layers = 1
        };

        VK_CHECK(vkCreateFramebuffer(logical_device, &create_info, nullptr, &framebuffers[i]));

        cleanup_queue.push([logical_device, framebuffer = framebuffers[i]]() { vkDestroyFramebuffer(logical_device, framebuffer, nullptr); });
    }

    return framebuffers;
}

auto create_command_pool(VkDevice logical_device, uint32_t queue_family_index, decltype(cleanup::general_queue)& cleanup_queue)
{
    const auto create_info = VkCommandPoolCreateInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queue_family_index
    };

    auto command_pool = VkCommandPool{};
    VK_CHECK(vkCreateCommandPool(logical_device, &create_info, nullptr, &command_pool));

    cleanup_queue.push([logical_device, command_pool]() { vkDestroyCommandPool(logical_device, command_pool, nullptr); });

    return command_pool;
}

auto create_command_buffers(VkDevice logical_device, VkCommandPool command_pool, uint32_t count, decltype(cleanup::general_queue)& cleanup_queue)
{
    auto command_buffers = std::vector<VkCommandBuffer>(count);

    const auto& allocate_info = VkCommandBufferAllocateInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = static_cast<uint32_t>(command_buffers.size())
    };

    VK_CHECK(vkAllocateCommandBuffers(logical_device, &allocate_info, command_buffers.data()));

    cleanup_queue.push([logical_device, command_pool, command_buffers]() { vkFreeCommandBuffers(logical_device, command_pool, command_buffers.size(), command_buffers.data()); });

    return command_buffers;
}

auto create_semaphores(VkDevice logical_device, uint32_t count, decltype(cleanup::general_queue)& cleanup_queue)
{
    auto semaphores = std::vector<VkSemaphore>(count);

    for (uint32_t i = 0; i < count; ++i)
    {
        const auto create_info = VkSemaphoreCreateInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0
        };

        auto semaphore = VkSemaphore{};
        VK_CHECK(vkCreateSemaphore(logical_device, &create_info, nullptr, &semaphore));

        cleanup_queue.push([logical_device, semaphore]() { vkDestroySemaphore(logical_device, semaphore, nullptr); });

        semaphores[i] = semaphore;
    }

    return semaphores;
}

auto create_fences(VkDevice logical_device, uint32_t count, decltype(cleanup::general_queue)& cleanup_queue)
{
    auto fences = std::vector<VkFence>(count);

    for (uint32_t i = 0; i < count; ++i)
    {
        const auto create_info = VkFenceCreateInfo{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT
        };

        auto fence = VkFence{};
        VK_CHECK(vkCreateFence(logical_device, &create_info, nullptr, &fence));

        cleanup_queue.push([logical_device, fence]() { vkDestroyFence(logical_device, fence, nullptr); });

        fences[i] = fence;
    }

    return fences;
}


auto find_memory_type_index(VkPhysicalDevice physical_device, uint32_t memory_type_requirements, VkMemoryPropertyFlags memory_property_flags)
{
    auto memory_properties = VkPhysicalDeviceMemoryProperties{};

    vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i)
    {
        const auto& memory_type = memory_properties.memoryTypes[i];

        if ( (memory_type_requirements & (1 << i)) && (memory_type.propertyFlags & memory_property_flags) == memory_property_flags)
        {
            return i;
        }
    }

    throw std::runtime_error("");
}

auto allocate_memory(VkDevice logical_device, std::size_t size, uint32_t memory_type_index, decltype(cleanup::general_queue)& cleanup_queue)
{
    const auto allocate_info = VkMemoryAllocateInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = size,
        .memoryTypeIndex = memory_type_index
    };

    auto memory = VkDeviceMemory{};
    vkAllocateMemory(logical_device, &allocate_info, nullptr, &memory);

    cleanup_queue.push([logical_device, memory]() { vkFreeMemory(logical_device, memory, nullptr); });

    return memory;
}

auto create_color_image(VkDevice logical_device, VkFormat swapchain_format, VkExtent2D swapchain_extent, decltype(cleanup::general_queue)& cleanup_queue)
{
        const auto create_info = VkImageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = swapchain_format,
            .extent = VkExtent3D{
                .width = swapchain_extent.width,
                .height = swapchain_extent.height,
                .depth = 1
            },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = msaa_samples,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
        };

        auto image = VkImage{};
        VK_CHECK(vkCreateImage(logical_device, &create_info, nullptr, &image));

        cleanup_queue.push([logical_device, image]() { vkDestroyImage(logical_device, image, nullptr); });

        auto memory_requirements = VkMemoryRequirements{};
        vkGetImageMemoryRequirements(logical_device, image, &memory_requirements);

        return std::tuple{image, memory_requirements};
}

auto create_color_image(VkDevice logical_device, VkPhysicalDevice physical_device, VkFormat swapchain_format, VkExtent2D swapchain_extent, decltype(cleanup::general_queue)& cleanup_queue)
{
    const auto [image, memory_requirements] = create_color_image(logical_device, swapchain_format, swapchain_extent, cleanup_queue);

    const auto memory = allocate_memory(logical_device, memory_requirements.size, find_memory_type_index(physical_device, memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), cleanup_queue);
    VK_CHECK(vkBindImageMemory(logical_device, image, memory, 0));

    const auto view = create_color_image_view(logical_device, swapchain_format, image, cleanup_queue);

    return std::tuple{image, view, memory};
}

auto create_depth_image(VkDevice logical_device, VkExtent2D swapchain_extent, decltype(cleanup::general_queue)& cleanup_queue)
{
    const auto create_info = VkImageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = depth_format,
        .extent = VkExtent3D{
            .width = swapchain_extent.width,
            .height = swapchain_extent.height,
            .depth = 1
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = msaa_samples,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    auto image = VkImage{};
    VK_CHECK(vkCreateImage(logical_device, &create_info, nullptr, &image));

    cleanup_queue.push([logical_device, image]() { vkDestroyImage(logical_device, image, nullptr); });

    auto memory_requirements = VkMemoryRequirements{};
    vkGetImageMemoryRequirements(logical_device, image, &memory_requirements);

    return std::tuple{image, memory_requirements};
}

auto create_depth_image_view(VkDevice logical_device, VkImage image, decltype(cleanup::general_queue)& cleanup_queue)
{
    const auto create_info = VkImageViewCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = depth_format,
        .components = VkComponentMapping{
            .r = VK_COMPONENT_SWIZZLE_IDENTITY,
            .g = VK_COMPONENT_SWIZZLE_IDENTITY,
            .b = VK_COMPONENT_SWIZZLE_IDENTITY,
            .a = VK_COMPONENT_SWIZZLE_IDENTITY,
        },
        .subresourceRange = VkImageSubresourceRange{
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    auto view = VkImageView{};
    VK_CHECK(vkCreateImageView(logical_device, &create_info, nullptr, &view));

    cleanup_queue.push([logical_device, view]() { vkDestroyImageView(logical_device, view, nullptr); });

    return view;
}

auto create_depth_image(VkDevice logical_device, VkPhysicalDevice physical_device, VkExtent2D swapchain_extent, decltype(cleanup::general_queue)& cleanup_queue)
{
    auto [image, mem_reqs] = create_depth_image(logical_device, swapchain_extent, cleanup_queue);
    auto memory = allocate_memory(logical_device, mem_reqs.size, find_memory_type_index(physical_device, mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), cleanup_queue);
    VK_CHECK(vkBindImageMemory(logical_device, image, memory, 0));
    auto view = create_depth_image_view(logical_device, image, cleanup_queue);

    return std::tuple{ image, view, memory };
}

void handle_keyboard(GLFWwindow* window, camera& camera)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE))
    {
        glfwSetWindowShouldClose(window, true);
    }

    if (glfwGetKey(window, GLFW_KEY_W))
    {
        camera.move_forward();
    }

    if (glfwGetKey(window, GLFW_KEY_S))
    {
        camera.move_back();
    }

    if (glfwGetKey(window, GLFW_KEY_A))
    {
        camera.strafe_left();
    }

    if (glfwGetKey(window, GLFW_KEY_D))
    {
        camera.strafe_right();
    }
}

auto create_buffer(VkDevice logical_device, std::size_t size, VkBufferUsageFlags usage, decltype(cleanup::general_queue)& cleanup_queue)
{
    const auto create_info = VkBufferCreateInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr
    };

    auto buffer = VkBuffer{};
    VK_CHECK(vkCreateBuffer(logical_device, &create_info, nullptr, &buffer));

    cleanup_queue.push([logical_device, buffer]() { vkDestroyBuffer(logical_device, buffer, nullptr); });

    auto memory_requirements = VkMemoryRequirements{};
    vkGetBufferMemoryRequirements(logical_device, buffer, &memory_requirements);

    return std::tuple{buffer, memory_requirements};
}

auto create_buffer(VkDevice logical_device, VkPhysicalDevice physical_device, std::size_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_flags, decltype(cleanup::general_queue)& cleanup_queue)
{
    const auto [buffer, memory_requirements] = create_buffer(logical_device, size, usage, cleanup_queue);
    const auto memory = allocate_memory(logical_device, memory_requirements.size, find_memory_type_index(physical_device, memory_requirements.memoryTypeBits, memory_flags), cleanup_queue);

    VK_CHECK(vkBindBufferMemory(logical_device, buffer, memory, 0));

    return std::tuple{buffer, memory};
}

auto copy_memory(VkDevice logical_device, VkDeviceMemory device_memory, uint32_t offset, const void* in_data, std::size_t size)
{
    void* pData;
    VK_CHECK(vkMapMemory(logical_device, device_memory, offset, size, 0, &pData));

    std::memcpy(pData, in_data, size);

    vkUnmapMemory(logical_device, device_memory);
}

auto create_descriptor_sets_layouts(VkDevice logical_device, decltype(cleanup::general_queue)& cleanup_queue)
{
    // I've tried reflecting spirv to determine this stuff, but it made more problems than just creating it manually here and ensuring shaders comply
    const auto bindings = std::array{
        VkDescriptorSetLayoutBinding{
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr
        },
        VkDescriptorSetLayoutBinding{
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .pImmutableSamplers = nullptr
        }
    };

    const auto create_info = VkDescriptorSetLayoutCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = bindings.size(),
        .pBindings = bindings.data()
    };

    auto layout = VkDescriptorSetLayout{};
    VK_CHECK(vkCreateDescriptorSetLayout(logical_device, &create_info, nullptr, &layout));

    cleanup_queue.push([logical_device, layout]() { vkDestroyDescriptorSetLayout(logical_device, layout, nullptr); });

    return layout;
}

auto create_descriptor_pool(VkDevice logical_device, decltype(cleanup::general_queue)& cleanup_queue)
{
    const auto pool_size = VkDescriptorPoolSize{
        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 16 // TODO should be enough for now
    };

    const auto pool_sizes = std::array { pool_size };

    const auto create_info = VkDescriptorPoolCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 16, // TODO should be enough for now
        .poolSizeCount = pool_sizes.size(),
        .pPoolSizes = pool_sizes.data()
    };

    auto pool = VkDescriptorPool{};
    VK_CHECK(vkCreateDescriptorPool(logical_device, &create_info, nullptr, &pool));

    cleanup_queue.push([logical_device, pool]() { vkDestroyDescriptorPool(logical_device, pool, nullptr); });

    return pool;
}

auto allocate_descriptor_sets(VkDevice logical_device, const std::vector<VkDescriptorSetLayout>& in_set_layouts, const VkDescriptorPool& pool, std::size_t frame_overlap)
{
    auto set_layouts = std::vector<VkDescriptorSetLayout>();
    set_layouts.reserve(in_set_layouts.size() * frame_overlap);

    for (const auto& set_layout : in_set_layouts)
    {
        for (std::size_t i = 0; i < frame_overlap; ++i)
        {
            set_layouts.push_back(set_layout);
        }
    }

    const auto allocate_info = VkDescriptorSetAllocateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = nullptr,
        .descriptorPool = pool,
        .descriptorSetCount = static_cast<uint32_t>(set_layouts.size()),
        .pSetLayouts = set_layouts.data()
    };

    auto sets = std::vector<VkDescriptorSet>(set_layouts.size());
    VK_CHECK(vkAllocateDescriptorSets(logical_device, &allocate_info, sets.data()));

    return sets;
}

auto create_descriptor_update_template(VkDevice logical_device, VkDescriptorSetLayout set_layout, VkPipelineLayout pipeline_layout, decltype(cleanup::general_queue)& cleanup_queue)
{
    auto update_entries = std::array{
        VkDescriptorUpdateTemplateEntry {
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .offset = 0,
            .stride = 0
        },
        VkDescriptorUpdateTemplateEntry {
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .offset = sizeof(VkDescriptorBufferInfo), // TODO lmao, fix this shit. It's an offset in pData array of vkCmdUpdateDescriptorSetWithTemplate
            .stride = 0
        },
    };

    const auto create_info = VkDescriptorUpdateTemplateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .descriptorUpdateEntryCount = update_entries.size(),
        .pDescriptorUpdateEntries = update_entries.data(),
        .templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET,
        .descriptorSetLayout = set_layout,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .pipelineLayout = pipeline_layout,
        .set = 0
    };

    auto update_template = VkDescriptorUpdateTemplate{};
    VK_CHECK(vkCreateDescriptorUpdateTemplate(logical_device, &create_info, nullptr, &update_template));

    cleanup_queue.push([logical_device, update_template]() { vkDestroyDescriptorUpdateTemplate(logical_device, update_template, nullptr); });

    return update_template;
}

auto get_descriptor_buffer_infos(VkBuffer buffer, std::size_t size, std::size_t frame_overlap)
{
    auto infos = std::vector<VkDescriptorBufferInfo>(frame_overlap);
    for (std::size_t i = 0; i < frame_overlap; ++i)
    {
        infos[i] = VkDescriptorBufferInfo{
            .buffer = buffer,
            .offset = i * size,
            .range = size
        };
    }

    return infos;
}

auto pad_uniform_buffer_size(std::size_t original_size, std::size_t min_uniform_buffer_alignment)
{
    std::size_t aligned_size = original_size;

    if (min_uniform_buffer_alignment > 0)
    {
        aligned_size = (aligned_size + min_uniform_buffer_alignment - 1) & ~(min_uniform_buffer_alignment - 1);
    }

    return aligned_size;
}

namespace boids
{
    auto visual_range = 1.f;
    auto cohesion_weight = 0.001f;
    auto separation_weight = 0.001f;
    auto alignment_weight = 0.001f;

    auto steer(std::size_t index, const std::vector<cone_instance>& boids)
    {
        assert(index < boids.size());
        const auto& current_boid = boids[index];

        auto observed_boids = std::size_t{ 0 };
        auto avg_observable_cluster_position = glm::vec4(0);
        auto separation = glm::vec4(0);
        auto alignment = glm::vec4();
        for (std::size_t i = 0; i < boids.size(); ++i)
        {
            const auto& boid = boids[i];
            const auto distance = glm::distance(current_boid.position, boid.position);
            if (i != index && distance < visual_range) // TODO use distance2 to avoid paying for sqrt
            {
                observed_boids++;
                avg_observable_cluster_position += boid.position;
                separation += (current_boid.position - boid.position) / glm::abs(distance);
                alignment += boid.velocity;
            }
        }

        if (observed_boids)
        {
            avg_observable_cluster_position /= observed_boids;
            alignment /= observed_boids;
            const auto total_cohesion = (avg_observable_cluster_position - current_boid.position) * cohesion_weight;
            const auto total_separation = separation * separation_weight;
            const auto total_alignment = alignment * alignment_weight;
            return total_cohesion + total_separation + total_alignment;
        }
        else
        {
            return glm::vec4(0);
        }
    }
}

namespace gui
{
    auto model_scale = glm::vec3(0.5, 0.5, 0.5);
    auto model_speed = 0.1f;
    auto wall_force_weight = 0.1f;

    auto create_descriptor_pool(VkDevice logical_device, decltype(cleanup::general_queue)& cleanup_queue)
    {
        // from imgui vulkan_glfw example
        // this is far too much, I reckon
        const auto pool_sizes = std::array {
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
        };

        const auto create_info = VkDescriptorPoolCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
            .maxSets = 1000,
            .poolSizeCount = pool_sizes.size(),
            .pPoolSizes = pool_sizes.data()
        };

        auto pool = VkDescriptorPool{};
        VK_CHECK(vkCreateDescriptorPool(logical_device, &create_info, nullptr, &pool));

        cleanup_queue.push([logical_device, pool]() { vkDestroyDescriptorPool(logical_device, pool, nullptr); });

        return pool;
    }

    auto init(GLFWwindow* window, VkInstance vk_instance, VkDevice logical_device, VkPhysicalDevice physical_device, uint32_t queue_family_index, VkQueue queue, uint32_t images_count, VkRenderPass render_pass, VkSurfaceKHR surface, VkSurfaceFormatKHR surface_format, VkSwapchainKHR swapchain, VkCommandPool command_pool, VkCommandBuffer command_buffer, decltype(cleanup::general_queue)& cleanup_queue)
    {
        const auto descriptor_pool = gui::create_descriptor_pool(logical_device, cleanup_queue);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;

        ImGui::StyleColorsDark();
        ImGui_ImplGlfw_InitForVulkan(window, true);
        // TODO this is because of volk - https://github.com/ocornut/imgui/issues/4854 https://github.com/ocornut/imgui/pull/6582
        ImGui_ImplVulkan_LoadFunctions([](const char* function_name, void* vulkan_instance) { return vkGetInstanceProcAddr(*(reinterpret_cast<VkInstance*>(vulkan_instance)), function_name); }, &vk_instance);
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = vk_instance;
        init_info.PhysicalDevice = physical_device;
        init_info.Device = logical_device;
        init_info.QueueFamily = queue_family_index;
        init_info.Queue = queue;
        init_info.PipelineCache = VK_NULL_HANDLE;
        init_info.DescriptorPool = descriptor_pool;
        init_info.Subpass = 0;
        init_info.MinImageCount = images_count,
        init_info.ImageCount = images_count,
        init_info.MSAASamples = msaa_samples;
        init_info.Allocator = nullptr;
        init_info.CheckVkResultFn = [](VkResult r) { VK_CHECK(r); };
        ImGui_ImplVulkan_Init(&init_info, render_pass);

        auto imgui_vulkan_window = ImGui_ImplVulkanH_Window();
        imgui_vulkan_window.Surface = surface;
        imgui_vulkan_window.SurfaceFormat = surface_format;
        imgui_vulkan_window.PresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
        imgui_vulkan_window.Swapchain = swapchain;

        {
            VK_CHECK(vkResetCommandPool(logical_device, command_pool, 0));
            VkCommandBufferBeginInfo begin_info = {};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));

            ImGui_ImplVulkan_CreateFontsTexture(command_buffer);

            VkSubmitInfo end_info = {};
            end_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            end_info.commandBufferCount = 1;
            end_info.pCommandBuffers = &command_buffer;
            VK_CHECK(vkEndCommandBuffer(command_buffer));
            VK_CHECK(vkQueueSubmit(queue, 1, &end_info, VK_NULL_HANDLE));
            VK_CHECK(vkDeviceWaitIdle(logical_device));
            ImGui_ImplVulkan_DestroyFontUploadObjects();
        }

        cleanup_queue.push([]() {
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
        });
    }

    void draw(VkCommandBuffer command_buffer, std::span<cone_instance>& cones)
    {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Text("Camera");
        static constexpr auto vec3_format = FMT_COMPILE("({: .2f}, {: .2f}, {: .2f})");
        static constexpr auto vec4_format = FMT_COMPILE("({: .2f}, {: .2f}, {: .2f}, {: .2f})");
        static constexpr auto aligned_vectors_format = FMT_COMPILE("{: <10} {:>}");
        const auto pos_str = fmt::format(vec3_format, g_camera.position().x, g_camera.position().y, g_camera.position().z);
        const auto up_str = fmt::format(vec3_format, g_camera.up().x, g_camera.up().y, g_camera.up().z);
        const auto front_str = fmt::format(vec3_format, g_camera.front().x, g_camera.front().y, g_camera.front().z);
        const auto right_str = fmt::format(vec3_format, g_camera.right().x, g_camera.right().y, g_camera.right().z);
        ImGui::Text(fmt::format(aligned_vectors_format, "pos:", pos_str).c_str());
        ImGui::Text(fmt::format(aligned_vectors_format, "up:", up_str).c_str());
        ImGui::Text(fmt::format(aligned_vectors_format, "front:", front_str).c_str());
        ImGui::Text(fmt::format(aligned_vectors_format, "right:", right_str).c_str());
        ImGui::DragFloat("Speed", &model_speed, 0.001, -1.f, 1.f);

        ImGui::Text("Boids params");
        ImGui::Separator();
        ImGui::DragFloat("Cohesion", &boids::cohesion_weight, 0.001, 0.f, 1.f);
        ImGui::Separator();
        ImGui::DragFloat("Separation", &boids::separation_weight, 0.001f, 0.f, 1.f);
        ImGui::Separator();
        ImGui::DragFloat("Alignment", &boids::alignment_weight, 0.001f, 0.f, 1.f);
        ImGui::Separator();
        ImGui::DragFloat("Visual range", &boids::visual_range, 0.1f, 0.f, 30.f);
        ImGui::Separator();
        ImGui::DragFloat("Wall force", &wall_force_weight, 0.01f, 0.f, 1.f);

        if (ImGui::CollapsingHeader(fmt::format("Instances [{}]", cones.size()).c_str()))
        {
            for (std::size_t i = 0; i < cones.size(); ++i)
            {
                if (ImGui::TreeNode(fmt::format("Instance {}", i).c_str()))
                {
                    auto& cone = cones[i];
                    const auto pos_str = fmt::format(vec3_format, cone.position.x, cone.position.y, cone.position.z);
                    const auto dir_str = fmt::format(vec3_format, cone.direction.x, cone.direction.y, cone.direction.z);
                    const auto color_str = fmt::format(vec4_format, cone.color.x, cone.color.y, cone.color.z, cone.color.w);
                    const auto velocity_str = fmt::format(vec3_format, cone.velocity.x, cone.velocity.y, cone.velocity.z);
                    ImGui::Text(fmt::format(aligned_vectors_format, "pos:", pos_str).c_str());
                    ImGui::Text(fmt::format(aligned_vectors_format, "dir:", dir_str).c_str());
                    ImGui::Text(fmt::format(aligned_vectors_format, "velocity:", velocity_str).c_str());
                    ImGui::Text(fmt::format(aligned_vectors_format, "color:", color_str).c_str());
                    ImGui::SameLine();
                    ImGui::ColorEdit4("", &cone.color[0], ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
                    ImGui::TreePop();
                }
            }
        }

        //ImGui::ShowDemoWindow();

        ImGui::Render();
        ImDrawData* draw_data = ImGui::GetDrawData();
        ImGui_ImplVulkan_RenderDrawData(draw_data, command_buffer);
    }
}

auto load_shaders(VkDevice logical_device, std::initializer_list<std::string_view> shader_paths, decltype(cleanup::general_queue)& cleanup_queue)
{
    auto shader_modules = std::vector<VkShaderModule>();
    shader_modules.reserve(shader_paths.size());
    for (const auto shader_path : shader_paths)
    {
        shader_modules.push_back(create_shader_module(logical_device, read_file(shader_path), cleanup_queue));
    }

    return shader_modules;
}

namespace cone
{
    constexpr auto bindingDescription = VkVertexInputBindingDescription{
        .binding = 0,
        .stride = 2 * sizeof(glm::vec3),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    constexpr auto vertexAttributeDescriptions = std::array{
        VkVertexInputAttributeDescription{
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = 0
        },
        VkVertexInputAttributeDescription{
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = sizeof glm::vec3
        },
    };

    const auto vertex_input_state = VkPipelineVertexInputStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &bindingDescription,
        .vertexAttributeDescriptionCount = vertexAttributeDescriptions.size(),
        .pVertexAttributeDescriptions = vertexAttributeDescriptions.data()
    };

    constexpr auto input_assembly_state = VkPipelineInputAssemblyStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE
    };

    auto viewport = VkViewport{
        .x = 0.f,
        .y = 0.f,
        .width = 0.f,
        .height = 0.f,
        .minDepth = 0.f,
        .maxDepth = 1.f
    };

    auto scissors = VkRect2D{
        .offset = VkOffset2D{
            .x = 0,
            .y = 0,
        },
        .extent = {}
    };

    const auto viewport_state = VkPipelineViewportStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissors,
    };

    constexpr auto rasterization_state = VkPipelineRasterizationStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0.f,
        .depthBiasClamp = 0.f,
        .depthBiasSlopeFactor = 0.f,
        .lineWidth = 2.f
    };

    constexpr auto multisample_state = VkPipelineMultisampleStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .rasterizationSamples = msaa_samples,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.f,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE
    };

    constexpr auto depth_stencil_state = VkPipelineDepthStencilStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
        .front = {},
        .back = {},
        .minDepthBounds = 0.f,
        .maxDepthBounds = 1.f
    };

    constexpr auto color_blend_attachment_state = VkPipelineColorBlendAttachmentState{
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };

    const auto color_blend_state = VkPipelineColorBlendStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment_state,
        .blendConstants = {0.f, 0.f, 0.f, 0.f}
    };

    auto generate_vertex_data()
    {
        constexpr auto base_vertices_count = 12;
        constexpr auto angle_step = 2 * std::numbers::pi / base_vertices_count;

        auto vertices = std::vector<glm::vec3>(base_vertices_count + 2);
        vertices[0] = glm::vec3(0, 0, 0);
        for (std::size_t i = 0; i < base_vertices_count; ++i)
        {
            const auto angle = i * angle_step;
            vertices[i+1] = glm::vec3(std::cos(angle), 0.f, std::sin(angle));
        }
        vertices[vertices.size() - 1] = glm::vec3(0, 2.f, 0);

        const auto triangles_count = base_vertices_count * 2 * 3;
        auto indices = std::vector<uint16_t>(triangles_count);

        // side triangles
        for (std::size_t i = 0; i < base_vertices_count; ++i)
        {
            indices[3*i + 0] = 0;
            indices[3*i + 1] = i + 1;
            indices[3*i + 2] = i + 2;
        }

        indices[3 * base_vertices_count - 1] = 1;

        // base triangles
        for (std::size_t i = 0; i < base_vertices_count; ++i)
        {
            indices[3*i + 3*base_vertices_count + 0] = base_vertices_count + 1;
            indices[3*i + 3*base_vertices_count + 1] = i + 2;
            indices[3*i + 3*base_vertices_count + 2] = i + 1;
        }

        indices[3 * 2 * base_vertices_count - 2] = 1;

        auto triangle_vertex_buffer = std::vector<vertex>(vertices.size());
        for (std::size_t i = 0; i < vertices.size(); ++i)
        {
            triangle_vertex_buffer[i] = { vertices[i], { 0, 0, 0 } };
        }

        return std::tuple{ triangle_vertex_buffer, indices };
    }

    auto shader_stages = std::array{
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = 0,
            .pName = shader_entry_point.data(),
            .pSpecializationInfo = nullptr
        },
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = 0,
            .pName = shader_entry_point.data(),
            .pSpecializationInfo = nullptr
        }
    };

    auto get_pipeline_create_info(VkDevice logical_device, const std::vector<VkShaderModule>& shaders, VkPipelineLayout pipeline_layout, VkRenderPass render_pass, const VkExtent2D& window_extent)
    {
        assert(shaders.size() == shader_stages.size());
        for (std::size_t i = 0; i < shaders.size(); ++i)
            shader_stages[i].module = shaders[i];

        viewport.width = window_extent.width;
        viewport.height = window_extent.height;

        scissors.extent = window_extent;

        const auto create_info = VkGraphicsPipelineCreateInfo{
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stageCount = static_cast<uint32_t>(shader_stages.size()),
            .pStages = shader_stages.data(),
            .pVertexInputState = &vertex_input_state,
            .pInputAssemblyState = &input_assembly_state,
            .pTessellationState = nullptr,
            .pViewportState = &viewport_state,
            .pRasterizationState = &rasterization_state,
            .pMultisampleState = &multisample_state,
            .pDepthStencilState = &depth_stencil_state,
            .pColorBlendState = &color_blend_state,
            .pDynamicState = nullptr,
            .layout = pipeline_layout,
            .renderPass = render_pass,
            .subpass = 0,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = 0
        };

        return create_info;
    }

    auto generate_model_data(std::span<cone_instance>& cones, glm::vec3 min_range, glm::vec3 max_range)
    {
        auto rd = std::random_device{};
        auto gen = std::mt19937(rd());
        auto dis = std::uniform_real_distribution<>(-1., 1.);
        auto x_dis = std::uniform_real_distribution<>(min_range.x, max_range.x);
        auto y_dis = std::uniform_real_distribution<>(min_range.y, max_range.y);
        auto z_dis = std::uniform_real_distribution<>(min_range.z, max_range.z);

        for (auto& cone : cones)
        {
            cone.position = glm::vec4(x_dis(gen), y_dis(gen), z_dis(gen), 0.);
            cone.direction = glm::normalize(glm::vec4(dis(gen), dis(gen), dis(gen), 0.));
            cone.velocity = cone.direction;
        }
    }
}

namespace grid
{
    // TODO potentially vertex input could be refleted from shaders
    constexpr auto vertex_input_state = VkPipelineVertexInputStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .vertexBindingDescriptionCount = 0,
        .pVertexBindingDescriptions = nullptr,
        .vertexAttributeDescriptionCount = 0,
        .pVertexAttributeDescriptions = nullptr
    };

    constexpr auto input_assembly_state = VkPipelineInputAssemblyStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE
    };

    auto viewport = VkViewport{
        .x = 0.f,
        .y = 0.f,
        .width = 0.f,
        .height = 0.f,
        .minDepth = 0.f,
        .maxDepth = 1.f
    };

    auto scissors = VkRect2D{
        .offset = VkOffset2D{
            .x = 0,
            .y = 0,
        },
        .extent = {}
    };

    const auto viewport_state = VkPipelineViewportStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissors,
    };

    constexpr auto rasterization_state = VkPipelineRasterizationStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0.f,
        .depthBiasClamp = 0.f,
        .depthBiasSlopeFactor = 0.f,
        .lineWidth = 2.f
    };

    constexpr auto multisample_state = VkPipelineMultisampleStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .rasterizationSamples = msaa_samples,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.f,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE
    };

    constexpr auto depth_stencil_state = VkPipelineDepthStencilStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
        .front = {},
        .back = {},
        .minDepthBounds = 0.f,
        .maxDepthBounds = 1.f
    };

    constexpr auto color_blend_attachment_state = VkPipelineColorBlendAttachmentState{
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };

    const auto color_blend_state = VkPipelineColorBlendStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment_state,
        .blendConstants = {0.f, 0.f, 0.f, 0.f}
    };

    auto shader_stages = std::array{
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = 0,
            .pName = shader_entry_point.data(),
            .pSpecializationInfo = nullptr
        },
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = 0,
            .pName = shader_entry_point.data(),
            .pSpecializationInfo = nullptr
        }
    };

    auto get_pipeline_create_info(VkDevice logical_device, const std::vector<VkShaderModule>& shaders, VkPipelineLayout pipeline_layout, VkRenderPass render_pass, const VkExtent2D& window_extent)
    {
        assert(shaders.size() == shader_stages.size());
        for (std::size_t i = 0; i < shaders.size(); ++i)
            shader_stages[i].module = shaders[i];

        viewport.width = window_extent.width;
        viewport.height = window_extent.height;

        scissors.extent = window_extent;

        const auto create_info = VkGraphicsPipelineCreateInfo{
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stageCount = static_cast<uint32_t>(shader_stages.size()),
            .pStages = shader_stages.data(),
            .pVertexInputState = &vertex_input_state,
            .pInputAssemblyState = &input_assembly_state,
            .pTessellationState = nullptr,
            .pViewportState = &viewport_state,
            .pRasterizationState = &rasterization_state,
            .pMultisampleState = &multisample_state,
            .pDepthStencilState = &depth_stencil_state,
            .pColorBlendState = &color_blend_state,
            .pDynamicState = nullptr,
            .layout = pipeline_layout,
            .renderPass = render_pass,
            .subpass = 0,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = 0
        };

        return create_info;
    }
}

namespace aquarium
{
    constexpr float scale = 30.f;
    const auto min_range = glm::vec3(-scale, 0.f, -scale);
    const auto max_range = glm::vec3(scale, scale, scale);
    auto force_weight = 0.1;

    struct
    {
        glm::vec3 front = glm::vec3(0, 0, -1);
        glm::vec3 back = glm::vec3(0, 0, 1);
        glm::vec3 top = glm::vec3(0, -1, 0);
        glm::vec3 bottom = glm::vec3(0, 1, 0);
        glm::vec3 left = glm::vec3(1, 0, 0);
        glm::vec3 right = glm::vec3(-1, 0, 0);
    } const inward_faces_normals;

    std::tuple<bool, const glm::vec3&> check_collision(const glm::vec4& pos)
    {
        if (pos.x < min_range.x)
            return { true, inward_faces_normals.left };
        else if (pos.x > max_range.x)
            return { true, inward_faces_normals.right };
        else if (pos.y < min_range.y)
            return { true, inward_faces_normals.bottom };
        else if (pos.y > max_range.y)
            return { true, inward_faces_normals.top };
        else if (pos.z < min_range.z)
            return { true, inward_faces_normals.back };
        else if (pos.z > max_range.z)
            return { true, inward_faces_normals.front };

        return { false, {} };
    }

    auto steer_away_from_walls(const cone_instance& boid)
    {
        const auto front_distance = glm::distance2(glm::vec3(boid.position), glm::vec3(boid.position.x, boid.position.y, max_range.z));
        const auto back_distance = glm::distance2(glm::vec3(boid.position), glm::vec3(boid.position.x, boid.position.y, min_range.z));
        const auto top_distance = glm::distance2(glm::vec3(boid.position), glm::vec3(boid.position.x, max_range.y, boid.position.z));
        const auto bottom_distance = glm::distance2(glm::vec3(boid.position), glm::vec3(boid.position.x, min_range.y, boid.position.z));
        const auto left_distance = glm::distance2(glm::vec3(boid.position), glm::vec3(min_range.x, boid.position.y, boid.position.z));
        const auto right_distance = glm::distance2(glm::vec3(boid.position), glm::vec3(max_range.x, boid.position.y, boid.position.z));

        auto velocity_diff = glm::vec3(0);

        velocity_diff += inward_faces_normals.front / front_distance;
        velocity_diff += inward_faces_normals.back / back_distance;
        velocity_diff += inward_faces_normals.top / top_distance;
        velocity_diff += inward_faces_normals.bottom / bottom_distance;
        velocity_diff += inward_faces_normals.left / left_distance;
        velocity_diff += inward_faces_normals.right / right_distance;

        return velocity_diff * gui::wall_force_weight;
    }

    constexpr auto vertex_input_state = VkPipelineVertexInputStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .vertexBindingDescriptionCount = 0,
        .pVertexBindingDescriptions = nullptr,
        .vertexAttributeDescriptionCount = 0,
        .pVertexAttributeDescriptions = nullptr
    };

    constexpr auto input_assembly_state = VkPipelineInputAssemblyStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE
    };

    auto viewport = VkViewport{
        .x = 0.f,
        .y = 0.f,
        .width = 0.f,
        .height = 0.f,
        .minDepth = 0.f,
        .maxDepth = 1.f
    };

    auto scissors = VkRect2D{
        .offset = VkOffset2D{
            .x = 0,
            .y = 0,
        },
        .extent = {}
    };

    const auto viewport_state = VkPipelineViewportStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissors,
    };

    constexpr auto rasterization_state = VkPipelineRasterizationStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_LINE,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0.f,
        .depthBiasClamp = 0.f,
        .depthBiasSlopeFactor = 0.f,
        .lineWidth = 10.f
    };

    constexpr auto multisample_state = VkPipelineMultisampleStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .rasterizationSamples = msaa_samples,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.f,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE
    };

    constexpr auto depth_stencil_state = VkPipelineDepthStencilStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
        .front = {},
        .back = {},
        .minDepthBounds = 0.f,
        .maxDepthBounds = 1.f
    };

    constexpr auto color_blend_attachment_state = VkPipelineColorBlendAttachmentState{
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };

    const auto color_blend_state = VkPipelineColorBlendStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment_state,
        .blendConstants = {0.f, 0.f, 0.f, 0.f}
    };

    auto shader_stages = std::array{
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = 0,
            .pName = shader_entry_point.data(),
            .pSpecializationInfo = nullptr
        },
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = 0,
            .pName = shader_entry_point.data(),
            .pSpecializationInfo = nullptr
        }
    };

    auto get_pipeline_create_info(VkDevice logical_device, const std::vector<VkShaderModule>& shaders, VkPipelineLayout pipeline_layout, VkRenderPass render_pass, const VkExtent2D& window_extent)
    {
        assert(shaders.size() == shader_stages.size());
        for (std::size_t i = 0; i < shaders.size(); ++i)
            shader_stages[i].module = shaders[i];

        viewport.width = window_extent.width;
        viewport.height = window_extent.height;

        scissors.extent = window_extent;

        const auto create_info = VkGraphicsPipelineCreateInfo{
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stageCount = static_cast<uint32_t>(shader_stages.size()),
            .pStages = shader_stages.data(),
            .pVertexInputState = &vertex_input_state,
            .pInputAssemblyState = &input_assembly_state,
            .pTessellationState = nullptr,
            .pViewportState = &viewport_state,
            .pRasterizationState = &rasterization_state,
            .pMultisampleState = &multisample_state,
            .pDepthStencilState = &depth_stencil_state,
            .pColorBlendState = &color_blend_state,
            .pDynamicState = nullptr,
            .layout = pipeline_layout,
            .renderPass = render_pass,
            .subpass = 0,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = 0
        };

        return create_info;
    }
}

auto recreate_graphics_pipeline_and_swapchain(GLFWwindow* window, VkDevice logical_device, VkPhysicalDevice physical_device, const std::vector<std::vector<VkShaderModule>>& shader_modules, VkPipelineLayout pipeline_layout, VkRenderPass render_pass, VkSurfaceKHR surface, uint32_t queue_family_index, VkFormat swapchain_format, decltype(cleanup::general_queue)& cleanup_queue)
{
    const auto window_extent = window::get_extent(window);
    spdlog::info("New extent: {}, {}", window_extent.width, window_extent.height);

    assert(shader_modules.size() == 3);
    const auto& cone_shaders = shader_modules[0];
    const auto& grid_shaders = shader_modules[1];
    const auto& aquarium_shaders = shader_modules[2];

    auto graphics_pipelines = create_graphics_pipelines(logical_device, {
        cone::get_pipeline_create_info(logical_device, cone_shaders, pipeline_layout, render_pass, window_extent),
        grid::get_pipeline_create_info(logical_device, grid_shaders, pipeline_layout, render_pass, window_extent),
        aquarium::get_pipeline_create_info(logical_device, aquarium_shaders, pipeline_layout, render_pass, window_extent),
    }, cleanup_queue);
    const auto [swapchain, surface_format] = create_swapchain(logical_device, physical_device, surface, queue_family_index, window_extent, cleanup_queue);
    const auto [swapchain_images, swapchain_image_views] = get_swapchain_images(logical_device, swapchain, surface_format.format, cleanup_queue);

    auto [color_image, color_image_view, color_image_memory] = create_color_image(logical_device, physical_device, swapchain_format, window_extent, cleanup_queue);
    auto [depth_image, depth_image_view, depth_image_memory] = create_depth_image(logical_device, physical_device, window_extent, cleanup_queue);

    const auto swapchain_framebuffers = create_swapchain_framebuffers(logical_device, render_pass, { color_image_view }, swapchain_image_views, { depth_image_view }, window_extent, cleanup_queue);

    return std::tuple{ graphics_pipelines, window_extent, swapchain, surface_format, swapchain_images, swapchain_image_views, swapchain_framebuffers, color_image, color_image_memory, color_image_view, depth_image, depth_image_view, depth_image_memory };
}

int main()
{
    spdlog::set_level(spdlog::level::trace);
    spdlog::info("Start");
    VK_CHECK(volkInitialize());

    const auto window = window::create(cleanup::general_queue);

    const auto requested_instance_layers = VALIDATION_LAYERS ? std::vector<const char*>{ "VK_LAYER_KHRONOS_validation" } : std::vector<const char*>{};
    const auto required_device_extensions = std::vector<const char*>{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    if (VALIDATION_LAYERS)
    {
        const auto instance_layers_found = check_instance_layers(requested_instance_layers);
        if (!instance_layers_found)
        {
            return 1;
        }
    }

    auto requested_extensions = window::get_vk_extensions();
    requested_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    const auto vk_instance = create_vulkan_instance(requested_instance_layers, requested_extensions, cleanup::general_queue);
    volkLoadInstance(vk_instance);

    create_debug_utils_messenger(vk_instance, cleanup::general_queue);

    const auto surface = window::create_vk_surface(vk_instance, window, cleanup::general_queue);

    const auto [physical_device, queue_family_index, physical_device_properties] = pick_physical_device(vk_instance, surface, required_device_extensions);
    const auto [logical_device, present_queue] = create_logical_device(physical_device, queue_family_index, required_device_extensions, cleanup::general_queue);

    auto window_extent = window::get_extent(window);

    auto [swapchain, surface_format] = create_swapchain(logical_device, physical_device, surface, queue_family_index, window_extent, cleanup::swapchain_queue);
    auto [swapchain_images, swapchain_image_views] = get_swapchain_images(logical_device, swapchain, surface_format.format, cleanup::swapchain_queue);

    const auto render_pass = create_render_pass(logical_device, surface_format.format, depth_format, msaa_samples, cleanup::general_queue);
    const auto descriptor_set_layout = create_descriptor_sets_layouts(logical_device, cleanup::general_queue);
    const auto pipeline_layout = create_pipeline_layout(logical_device, { descriptor_set_layout }, cleanup::general_queue);
    const auto cone_shaders = load_shaders(logical_device, { shader_path::vertex::triangle, shader_path::fragment::triangle }, cleanup::general_queue);
    const auto grid_shaders = load_shaders(logical_device, { shader_path::vertex::grid, shader_path::fragment::grid }, cleanup::general_queue);
    const auto aquarium_shaders = load_shaders(logical_device, { shader_path::vertex::aquarium, shader_path::fragment::aquarium }, cleanup::general_queue);

    auto graphics_pipelines = create_graphics_pipelines(logical_device, {
        cone::get_pipeline_create_info(logical_device, cone_shaders, pipeline_layout, render_pass, window_extent),
        grid::get_pipeline_create_info(logical_device, grid_shaders, pipeline_layout, render_pass, window_extent),
        aquarium::get_pipeline_create_info(logical_device, aquarium_shaders, pipeline_layout, render_pass, window_extent),
    }, cleanup::swapchain_queue);

    auto& cone_pipeline = graphics_pipelines[0];
    auto& grid_pipeline = graphics_pipelines[1];
    auto& aquarium_pipeline = graphics_pipelines[2];

    constexpr auto overlapping_frames_count = 2;

    const auto descriptor_pool = create_descriptor_pool(logical_device,  cleanup::general_queue);
    const auto descriptor_sets = allocate_descriptor_sets(logical_device, { descriptor_set_layout }, descriptor_pool, overlapping_frames_count);
    const auto descriptor_update_template = create_descriptor_update_template(logical_device, descriptor_set_layout, pipeline_layout, cleanup::general_queue);

    struct
    {
        glm::vec4 position;
        glm::mat4 viewproj;
    } camera_data;

    constexpr auto instances_count = 100;
    cone_instance model_data[instances_count];
    auto model_data_update_buffer = std::vector<cone_instance>(instances_count);

    auto model_data_span = std::span(model_data, model_data + instances_count);
    cone::generate_model_data(model_data_span, aquarium::min_range, aquarium::max_range);

    const auto camera_data_padded_size = pad_uniform_buffer_size(sizeof(camera_data), physical_device_properties.limits.minUniformBufferOffsetAlignment);
    const auto [camera_data_buffer, camera_data_memory] = create_buffer(logical_device, physical_device, overlapping_frames_count * camera_data_padded_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, cleanup::general_queue);
    void* camera_data_memory_ptr = nullptr;
    VK_CHECK(vkMapMemory(logical_device, camera_data_memory, 0, VK_WHOLE_SIZE, 0, &camera_data_memory_ptr));
    const auto camera_data_descriptor_buffer_infos = get_descriptor_buffer_infos(camera_data_buffer, camera_data_padded_size, overlapping_frames_count);

    const auto model_data_padded_size = pad_uniform_buffer_size(sizeof(model_data), physical_device_properties.limits.minUniformBufferOffsetAlignment);
    const auto [model_data_buffer, model_data_memory] = create_buffer(logical_device, physical_device, overlapping_frames_count * model_data_padded_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, cleanup::general_queue);
    void* model_data_memory_ptr = nullptr;
    VK_CHECK(vkMapMemory(logical_device, model_data_memory, 0, VK_WHOLE_SIZE, 0, &model_data_memory_ptr));
    const auto model_data_descriptor_buffer_infos = get_descriptor_buffer_infos(model_data_buffer, model_data_padded_size, overlapping_frames_count);

    auto [color_image, color_image_view, color_image_memory] = create_color_image(logical_device, physical_device, surface_format.format, window_extent, cleanup::swapchain_queue);
    auto [depth_image, depth_image_view, depth_image_memory] = create_depth_image(logical_device, physical_device, window_extent, cleanup::swapchain_queue);

    assert(swapchain_image_views.size() == 1); // TODO create color images per each swapchain image?

    auto swapchain_framebuffers = create_swapchain_framebuffers(logical_device, render_pass, { color_image_view }, swapchain_image_views, { depth_image_view }, window_extent, cleanup::swapchain_queue);

    const auto command_pool = create_command_pool(logical_device, queue_family_index, cleanup::general_queue);
    const auto command_buffers = create_command_buffers(logical_device, command_pool, overlapping_frames_count, cleanup::general_queue);

    const auto [cone_vertex_buffer, cone_index_buffer] = cone::generate_vertex_data();
    const auto cone_vertex_buffer_size = cone_vertex_buffer.size() * sizeof(vertex);
    const auto cone_index_buffer_size = cone_index_buffer.size() * sizeof(decltype(cone_index_buffer)::value_type);

    const auto [vertex_buffer, device_memory] = create_buffer(logical_device, physical_device, cone_vertex_buffer_size + cone_index_buffer_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, cleanup::general_queue);
    copy_memory(logical_device, device_memory, 0, cone_vertex_buffer.data(), cone_vertex_buffer_size);
    copy_memory(logical_device, device_memory, cone_vertex_buffer_size, cone_index_buffer.data(), cone_index_buffer_size);

    const auto image_available_semaphores = create_semaphores(logical_device, overlapping_frames_count, cleanup::general_queue);
    const auto rendering_finished_semaphores = create_semaphores(logical_device, overlapping_frames_count, cleanup::general_queue);
    const auto overlapping_frames_fences = create_fences(logical_device, overlapping_frames_count, cleanup::general_queue);

    gui::init(window, vk_instance, logical_device, physical_device, queue_family_index, present_queue, overlapping_frames_count, render_pass, surface, surface_format, swapchain, command_pool, command_buffers[0], cleanup::general_queue);

    spdlog::trace("Entering main loop.");
    auto current_frame = uint32_t{ 0 };
    auto image_index = uint32_t{ 0 };
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        handle_keyboard(window, g_camera);

        const auto fence = overlapping_frames_fences[current_frame];
        const auto image_available_semaphore = image_available_semaphores[current_frame];
        const auto rendering_finished_semaphore = rendering_finished_semaphores[current_frame];
        const auto command_buffer = command_buffers[current_frame];

        VK_CHECK(vkWaitForFences(logical_device, 1, &fence, VK_TRUE, UINT64_MAX));

        {
            const auto result = vkAcquireNextImageKHR(logical_device, swapchain, UINT64_MAX, image_available_semaphore, VK_NULL_HANDLE, &image_index);
            if (result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR)
            {
                spdlog::info("Swapchain images no longer match native surface properties. Recreating swapchain.");
                VK_CHECK(vkDeviceWaitIdle(logical_device));

                spdlog::info("Destroy swapchain objects.");
                cleanup::flush(cleanup::swapchain_queue);

                std::tie(graphics_pipelines, window_extent, swapchain, surface_format, swapchain_images, swapchain_image_views, swapchain_framebuffers, color_image, color_image_memory, color_image_view, depth_image, depth_image_view, depth_image_memory) = recreate_graphics_pipeline_and_swapchain(window, logical_device, physical_device, { cone_shaders, grid_shaders, aquarium_shaders }, pipeline_layout, render_pass, surface, queue_family_index, surface_format.format, cleanup::swapchain_queue);
                cone_pipeline = graphics_pipelines[0];
                grid_pipeline = graphics_pipelines[1];
                aquarium_pipeline = graphics_pipelines[2];
                continue;
            }
            else if (result != VK_SUCCESS)
            {
                throw std::runtime_error("");
            }
        }

        VK_CHECK(vkResetFences(logical_device, 1, &fence));

        const auto begin_info = VkCommandBufferBeginInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr
        };

        VK_CHECK(vkResetCommandBuffer(command_buffer, 0));
        VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));

        const auto clear_values = std::array{
            VkClearValue{
                .color = VkClearColorValue{
                    .float32 = { 130.f / 255.f, 163.f / 255.f, 255.f / 255.f }
                }
            },
            VkClearValue{
                .depthStencil = VkClearDepthStencilValue{
                    .depth = 1.f,
                    .stencil = 0
                }
            }
        };

        camera_data.position = glm::vec4(g_camera.position(), 0.f);
        camera_data.viewproj = flip_clip_space * g_camera.projection(window_extent.width, window_extent.height) * g_camera.view();
        std::memcpy(reinterpret_cast<char*>(camera_data_memory_ptr) + current_frame * camera_data_padded_size, &camera_data, sizeof(camera_data));
        model_data_update_buffer = std::vector(model_data_span.begin(), model_data_span.end());
        // TODO flush buffer before descriptor set update?
        for (std::size_t i = 0; i < instances_count; ++i)
        {
            auto& model = model_data[i];
            const auto velocity_update1 = boids::steer(i, model_data_update_buffer);
            const auto velocity_update2 = glm::vec4(aquarium::steer_away_from_walls(model), 0);
            model.velocity = model.direction;
            model.velocity += velocity_update1 + velocity_update2;
            model.velocity *= gui::model_speed;
            if (glm::length(model.velocity))
                model.direction = glm::normalize(model.velocity);
            const auto& [collision, normal] = aquarium::check_collision(model.position + model.velocity);
            if (collision)
            {
                model.direction = glm::vec4(glm::reflect(glm::vec3(model.direction), normal), 0.);
            }
            else
            {
                model.position += model.velocity;
            }

            model.model_matrix = glm::translate(glm::mat4(1.), glm::vec3(model.position));
            model.model_matrix = model.model_matrix * glm::toMat4(glm::rotation({0, 1, 0}, glm::normalize(glm::vec3(model.direction))));
            model.model_matrix = glm::scale(model.model_matrix, gui::model_scale * glm::vec3(0.5));
        }
        std::memcpy(reinterpret_cast<char*>(model_data_memory_ptr) + current_frame * model_data_padded_size, &model_data, sizeof(model_data));

        const auto buffer_infos = std::array{
            camera_data_descriptor_buffer_infos[current_frame],
            model_data_descriptor_buffer_infos[current_frame]
        };
        vkUpdateDescriptorSetWithTemplate(logical_device, descriptor_sets[current_frame], descriptor_update_template, buffer_infos.data());

        const auto render_pass_begin_info = VkRenderPassBeginInfo{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext = nullptr,
            .renderPass = render_pass,
            .framebuffer = swapchain_framebuffers[image_index],
            .renderArea = VkRect2D {
                .offset = VkOffset2D { 0, 0 },
                .extent = window_extent
            },
            .clearValueCount = clear_values.size(),
            .pClearValues = clear_values.data()
        };

        vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdPushConstants(command_buffer, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float), &aquarium::scale);
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_sets[current_frame], 0, nullptr);
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, cone_pipeline);
        const auto offsets = std::array{ VkDeviceSize{ 0 } };
        vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer, offsets.data());
        vkCmdBindIndexBuffer(command_buffer, vertex_buffer, cone_vertex_buffer_size, VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(command_buffer, cone_index_buffer.size(), instances_count, 0, 0, 0);

        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, aquarium_pipeline);
        vkCmdDraw(command_buffer, 36, 1, 0, 0);

        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, grid_pipeline);
        vkCmdDraw(command_buffer, 6, 1, 0, 0);

        gui::draw(command_buffer, model_data_span);

        vkCmdEndRenderPass(command_buffer);
        VK_CHECK(vkEndCommandBuffer(command_buffer));

        const auto wait_semaphores = std::array{image_available_semaphore};
        const auto wait_stages = VkPipelineStageFlags{VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        const auto signal_semaphores = std::array{rendering_finished_semaphore};

        const auto submit_info = VkSubmitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = wait_semaphores.data(),
            .pWaitDstStageMask = &wait_stages,
            .commandBufferCount = 1,
            .pCommandBuffers = &command_buffer,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = signal_semaphores.data(),
        };

        VK_CHECK(vkQueueSubmit(present_queue, 1, &submit_info, fence));

        const auto present_info = VkPresentInfoKHR{
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = nullptr,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = signal_semaphores.data(),
            .swapchainCount = 1,
            .pSwapchains = &swapchain,
            .pImageIndices = &image_index,
            .pResults = nullptr,
        };

        VK_CHECK(vkQueuePresentKHR(present_queue, &present_info));

        current_frame = (current_frame + 1) % overlapping_frames_count;
    }

    VK_CHECK(vkDeviceWaitIdle(logical_device));

    spdlog::trace("Cleanup.");

    cleanup::flush(cleanup::swapchain_queue);
    cleanup::flush(cleanup::general_queue);
}
