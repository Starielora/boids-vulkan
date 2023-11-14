#include "setup.hpp"
#include "vkcheck.hpp"
#include "constants.hpp"

#include <set>
#include <string_view>
#include <algorithm>
#include <array>
#include <fstream>

namespace window
{
    GLFWwindow* create(cleanup::queue_type& cleanup_queue, const GLFWcursorposfun& mouse_callback, GLFWkeyfun key_callback)
    {
        spdlog::trace("Initialize glfw.");
        const auto glfw_initialized = glfwInit();
        assert(glfw_initialized == GLFW_TRUE);

        spdlog::trace("Create glfw window.");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        glfwWindowHint(GLFW_MAXIMIZED, GL_TRUE);

        auto* const window = glfwCreateWindow(800, 600, "boids", nullptr, nullptr);
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

    std::vector<const char*> get_vk_extensions()
    {
        auto glfw_extensions_count = uint32_t{ 0 };
        const auto** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extensions_count);
        return std::vector<const char*>(glfw_extensions, glfw_extensions + glfw_extensions_count);
    }

    VkSurfaceKHR create_vk_surface(VkInstance vk_instance, GLFWwindow* window, cleanup::queue_type& cleanup_queue)
    {
        auto surface = VkSurfaceKHR{ 0 };
        VK_CHECK(glfwCreateWindowSurface(vk_instance, window, nullptr, &surface));

        cleanup_queue.push([vk_instance, surface]() { vkDestroySurfaceKHR(vk_instance, surface, nullptr); });

        return surface;
    }

    VkExtent2D get_extent(GLFWwindow* window)
    {
        int glfw_fb_extent_width, glfw_fb_extent_height;
        glfwGetFramebufferSize(window, &glfw_fb_extent_width, &glfw_fb_extent_height);
        return VkExtent2D{ static_cast<uint32_t>(glfw_fb_extent_width), static_cast<uint32_t>(glfw_fb_extent_height) };
    }
}

constexpr auto debug_utils_messenger_create_info = VkDebugUtilsMessengerCreateInfoEXT{
    .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
    .pNext = nullptr,
    .flags = 0,
    .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT,
    .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
    .pfnUserCallback = &debug_callback,
    .pUserData = nullptr
};

constexpr auto enabled_features = std::array{ VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT };
const auto validation_features = VkValidationFeaturesEXT{
    .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
    .pNext = &debug_utils_messenger_create_info,
    .enabledValidationFeatureCount = enabled_features.size(),
    .pEnabledValidationFeatures = enabled_features.data(),
    .disabledValidationFeatureCount = 0,
    .pDisabledValidationFeatures = nullptr
};

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
    const auto message = pCallbackData->pMessageIdName != nullptr ? fmt::format("{} ({}): {}", type, pCallbackData->pMessageIdName, pCallbackData->pMessage) : fmt::format("{}: {}", type, pCallbackData->pMessage);

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

void create_debug_utils_messenger(VkInstance vk_instance, cleanup::queue_type& cleanup_queue)
{
    auto debug_messenger = VkDebugUtilsMessengerEXT{ 0 };
    VK_CHECK(vkCreateDebugUtilsMessengerEXT(vk_instance, &debug_utils_messenger_create_info, nullptr, &debug_messenger));

    cleanup_queue.push([vk_instance, debug_messenger]() { vkDestroyDebugUtilsMessengerEXT(vk_instance, debug_messenger, nullptr); });
}

VkInstance create_vulkan_instance(const std::vector<const char*>& layers, const std::vector<const char*>& extensions, cleanup::queue_type& cleanup_queue)
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
        .pNext = &validation_features,
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

bool check_instance_layers(const std::vector<const char*>& requested_layers)
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

std::tuple<VkPhysicalDevice, uint32_t, VkPhysicalDeviceProperties> pick_physical_device(VkInstance instance, VkSurfaceKHR surface, const std::vector<const char*> required_device_extensions)
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

std::tuple<VkDevice, VkQueue> create_logical_device(VkPhysicalDevice physical_device, uint32_t queue_family_index, const std::vector<const char*>& device_extensions, cleanup::queue_type& cleanup_queue)
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

std::tuple<VkSwapchainKHR, VkSurfaceFormatKHR> create_swapchain(VkDevice logical_device, VkPhysicalDevice physical_device, VkSurfaceKHR surface, uint32_t queue_family_index, VkExtent2D glfw_framebuffer_extent, cleanup::queue_type& cleanup_queue)
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

VkImageView create_color_image_view(VkDevice logical_device, VkFormat format, VkImage image, cleanup::queue_type& cleanup_queue)
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

std::tuple<std::vector<VkImage>, std::vector<VkImageView>> get_swapchain_images(VkDevice logical_device, VkSwapchainKHR swapchain, VkFormat image_format, cleanup::queue_type& cleanup_queue)
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

VkRenderPass create_render_pass(VkDevice logical_device, VkFormat swapchain_format, VkFormat depth_format, VkSampleCountFlagBits samples, cleanup::queue_type& cleanup_queue)
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

VkPipelineLayout create_pipeline_layout(VkDevice logical_device, const std::vector<VkDescriptorSetLayout>& set_layouts, cleanup::queue_type& cleanup_queue)
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

std::vector<VkPipeline> create_graphics_pipelines(VkDevice logical_device, const std::vector<VkGraphicsPipelineCreateInfo>& create_infos, cleanup::queue_type& cleanup_queue)
{
    auto pipelines = std::vector<VkPipeline>(create_infos.size());
    VK_CHECK(vkCreateGraphicsPipelines(logical_device, VK_NULL_HANDLE, create_infos.size(), create_infos.data(), nullptr, pipelines.data()));

    cleanup_queue.push([logical_device, pipelines]() {
        for (const auto pipeline : pipelines)
        {
            vkDestroyPipeline(logical_device, pipeline, nullptr);
        }
    });
    return pipelines;
}

VkPipeline create_boids_update_compute_pipeline(VkDevice logical_device, const VkComputePipelineCreateInfo& create_info, cleanup::queue_type& cleanup_queue)
{
    auto pipeline = VkPipeline{};
    VK_CHECK(vkCreateComputePipelines(logical_device, VK_NULL_HANDLE, 1, &create_info, nullptr, &pipeline));

    cleanup_queue.push([logical_device, pipeline](){
        vkDestroyPipeline(logical_device, pipeline, nullptr);
    });

    return pipeline;
}

std::vector<VkFramebuffer> create_swapchain_framebuffers(VkDevice logical_device, VkRenderPass render_pass, const std::vector<VkImageView>& color_imageviews, const std::vector<VkImageView>& swapchain_imageviews, const std::vector<VkImageView> depth_image_views, VkExtent2D swapchain_extent, cleanup::queue_type& cleanup_queue)
{
    auto framebuffers = std::vector<VkFramebuffer>(swapchain_imageviews.size());

    for (std::size_t i = 0; i < swapchain_imageviews.size(); ++i)
    {
        // TODO this probably will bite me later, if color or depth are used for any read op
        const auto attachments = std::array{ color_imageviews[0], depth_image_views[0], swapchain_imageviews[i] };
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

VkCommandPool create_command_pool(VkDevice logical_device, uint32_t queue_family_index, cleanup::queue_type& cleanup_queue)
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

std::vector<VkCommandBuffer> create_command_buffers(VkDevice logical_device, VkCommandPool command_pool, uint32_t count, cleanup::queue_type& cleanup_queue)
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

std::vector<VkSemaphore> create_semaphores(VkDevice logical_device, uint32_t count, cleanup::queue_type& cleanup_queue)
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

std::vector<VkFence> create_fences(VkDevice logical_device, uint32_t count, cleanup::queue_type& cleanup_queue)
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

uint32_t find_memory_type_index(VkPhysicalDevice physical_device, uint32_t memory_type_requirements, VkMemoryPropertyFlags memory_property_flags)
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

VkDeviceMemory allocate_memory(VkDevice logical_device, std::size_t size, uint32_t memory_type_index, cleanup::queue_type& cleanup_queue)
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

std::tuple<VkImage, VkMemoryRequirements> create_color_image(VkDevice logical_device, VkFormat swapchain_format, VkExtent2D swapchain_extent, cleanup::queue_type& cleanup_queue)
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

std::tuple<VkImage, VkImageView, VkDeviceMemory> create_color_image(VkDevice logical_device, VkPhysicalDevice physical_device, VkFormat swapchain_format, VkExtent2D swapchain_extent, cleanup::queue_type& cleanup_queue)
{
    const auto& [image, memory_requirements] = create_color_image(logical_device, swapchain_format, swapchain_extent, cleanup_queue);

    const auto memory = allocate_memory(logical_device, memory_requirements.size, find_memory_type_index(physical_device, memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), cleanup_queue);
    VK_CHECK(vkBindImageMemory(logical_device, image, memory, 0));

    const auto view = create_color_image_view(logical_device, swapchain_format, image, cleanup_queue);

    return std::tuple{image, view, memory};
}

std::tuple<VkImage, VkMemoryRequirements> create_depth_image(VkDevice logical_device, VkExtent2D swapchain_extent, cleanup::queue_type& cleanup_queue)
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

VkImageView create_depth_image_view(VkDevice logical_device, VkImage image, cleanup::queue_type& cleanup_queue)
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

std::tuple<VkImage, VkImageView, VkDeviceMemory> create_depth_image(VkDevice logical_device, VkPhysicalDevice physical_device, VkExtent2D swapchain_extent, cleanup::queue_type& cleanup_queue)
{
    auto [image, mem_reqs] = create_depth_image(logical_device, swapchain_extent, cleanup_queue);
    auto memory = allocate_memory(logical_device, mem_reqs.size, find_memory_type_index(physical_device, mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), cleanup_queue);
    VK_CHECK(vkBindImageMemory(logical_device, image, memory, 0));
    auto view = create_depth_image_view(logical_device, image, cleanup_queue);

    return std::tuple{ image, view, memory };
}

std::tuple<VkBuffer, VkMemoryRequirements> create_buffer(VkDevice logical_device, std::size_t size, VkBufferUsageFlags usage, cleanup::queue_type& cleanup_queue)
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

std::tuple<VkBuffer, VkDeviceMemory> create_buffer(VkDevice logical_device, VkPhysicalDevice physical_device, std::size_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_flags, cleanup::queue_type& cleanup_queue)
{
    const auto& [buffer, memory_requirements] = create_buffer(logical_device, size, usage, cleanup_queue);
    const auto memory = allocate_memory(logical_device, memory_requirements.size, find_memory_type_index(physical_device, memory_requirements.memoryTypeBits, memory_flags), cleanup_queue);

    VK_CHECK(vkBindBufferMemory(logical_device, buffer, memory, 0));

    return std::tuple{buffer, memory};
}

void copy_memory(VkDevice logical_device, VkDeviceMemory device_memory, uint32_t offset, const void* in_data, std::size_t size)
{
    void* pData;
    VK_CHECK(vkMapMemory(logical_device, device_memory, offset, size, 0, &pData));

    std::memcpy(pData, in_data, size);

    vkUnmapMemory(logical_device, device_memory);
}

VkDescriptorSetLayout create_descriptor_sets_layouts(VkDevice logical_device, cleanup::queue_type& cleanup_queue)
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
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr
        },
        VkDescriptorSetLayoutBinding{
            .binding = 2,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr
        },
        VkDescriptorSetLayoutBinding{
            .binding = 3,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr
        },
        VkDescriptorSetLayoutBinding{
            .binding = 4,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr
        },
        VkDescriptorSetLayoutBinding{
            .binding = 5,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
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

VkDescriptorPool create_descriptor_pool(VkDevice logical_device, cleanup::queue_type& cleanup_queue)
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

std::vector<VkDescriptorSet> allocate_descriptor_sets(VkDevice logical_device, const std::vector<VkDescriptorSetLayout>& in_set_layouts, const VkDescriptorPool& pool, std::size_t frame_overlap)
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

VkDescriptorUpdateTemplate create_descriptor_update_template(VkDevice logical_device, VkDescriptorSetLayout set_layout, VkPipelineLayout pipeline_layout, cleanup::queue_type& cleanup_queue)
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
        VkDescriptorUpdateTemplateEntry {
            .dstBinding = 2,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .offset = 2*sizeof(VkDescriptorBufferInfo), // TODO lmao, fix this shit. It's an offset in pData array of vkCmdUpdateDescriptorSetWithTemplate
            .stride = 0
        },
        VkDescriptorUpdateTemplateEntry {
            .dstBinding = 3,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .offset = 3*sizeof(VkDescriptorBufferInfo), // TODO lmao, fix this shit. It's an offset in pData array of vkCmdUpdateDescriptorSetWithTemplate
            .stride = 0
        },
        VkDescriptorUpdateTemplateEntry {
            .dstBinding = 4,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .offset = 4*sizeof(VkDescriptorBufferInfo), // TODO lmao, fix this shit. It's an offset in pData array of vkCmdUpdateDescriptorSetWithTemplate
            .stride = 0
        }
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

std::vector<VkDescriptorBufferInfo> get_descriptor_buffer_infos(VkBuffer buffer, std::size_t size, std::size_t frame_overlap)
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

std::size_t pad_uniform_buffer_size(std::size_t original_size, std::size_t min_uniform_buffer_alignment)
{
    std::size_t aligned_size = original_size;

    if (min_uniform_buffer_alignment > 0)
    {
        aligned_size = (aligned_size + min_uniform_buffer_alignment - 1) & ~(min_uniform_buffer_alignment - 1);
    }

    return aligned_size;
}

