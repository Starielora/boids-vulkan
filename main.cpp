#define NOMINMAX
#include <iostream>

#define VK_NO_PROTOTYPES
#include <Volk/volk.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>

#include <spdlog/spdlog.h>
#include <shaders/shaders.h>

#include <cassert>
#include <vector>
#include <limits>
#include <optional>
#include <set>
#include <string_view>
#include <fstream>
#include <array>

// TODO error message
#define VK_CHECK(f) do { const auto result = f; if(result != VK_SUCCESS) throw std::runtime_error("");} while(0)

auto read_file(const std::string_view filename)
{
    spdlog::debug("Reading file: {}", filename);
    auto file = std::ifstream(filename.data(), std::ios::binary);
    if (!file.is_open())
    {
        spdlog::error("Could not open file {}", filename);
        throw std::runtime_error("");
    }
    return std::vector<char>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

auto create_glfw_window()
{
    spdlog::trace("Create glfw window.");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

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
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
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

VkShaderModule create_shader_module(VkDevice logical_device, const std::vector<char>& code)
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

    return shader_module;
}

auto create_graphics_pipeline(VkDevice logical_device, VkExtent2D swapchain_extent, VkFormat swapchain_format)
{
    spdlog::info("Loading shaders");
    const auto triangle_vertex_shader = create_shader_module(logical_device, read_file(shader_path::vertex::triangle));
    const auto triangle_fragment_shader = create_shader_module(logical_device, read_file(shader_path::fragment::triangle));

    const auto shader_stage_create_infos = std::array{
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = triangle_vertex_shader,
            .pName = "main",
            .pSpecializationInfo = nullptr
        },
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = triangle_fragment_shader,
            .pName = "main",
            .pSpecializationInfo = nullptr
        }
    };

    const auto bindingDescription = VkVertexInputBindingDescription{
        .binding = 0,
        .stride = 2 * sizeof(glm::vec3),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    const auto vertexAttributeDescriptions = std::array{
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

    const auto vertex_input_create_info = VkPipelineVertexInputStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &bindingDescription,
        .vertexAttributeDescriptionCount = vertexAttributeDescriptions.size(),
        .pVertexAttributeDescriptions = vertexAttributeDescriptions.data()
    };

    const auto input_assembly_create_info = VkPipelineInputAssemblyStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE
    };

    const auto viewport_state_create_info = VkPipelineViewportStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .viewportCount = 1,
        .pViewports = nullptr,
        .scissorCount = 1,
        .pScissors = nullptr,
    };

    const auto rasterization_state_create_info = VkPipelineRasterizationStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0.f,
        .depthBiasClamp = 0.f,
        .depthBiasSlopeFactor = 0.f,
        .lineWidth = 1.f
    };

    const auto multisample_state_create_info = VkPipelineMultisampleStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.f,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE
    };

    const auto color_blend_attachment_state = VkPipelineColorBlendAttachmentState{
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };

    const auto color_blend_state_create_info = VkPipelineColorBlendStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment_state,
        .blendConstants = {0.f, 0.f, 0.f, 0.f}
    };

    const auto pipeline_layout_create_info = VkPipelineLayoutCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .setLayoutCount = 0,
        .pSetLayouts = nullptr,
        .pushConstantRangeCount = 0,
        .pPushConstantRanges = nullptr
    };

    auto pipeline_layout = VkPipelineLayout{};
    VK_CHECK(vkCreatePipelineLayout(logical_device, &pipeline_layout_create_info, nullptr, &pipeline_layout));

    const auto color_attachment = VkAttachmentDescription{
        .flags = 0,
        .format = swapchain_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };

    const auto attachmentRef = VkAttachmentReference{
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };

    const auto subpass = VkSubpassDescription{
        .flags = 0,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .inputAttachmentCount = 0,
        .pInputAttachments = nullptr,
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachmentRef,
        .pResolveAttachments = 0,
        .pDepthStencilAttachment = nullptr,
        .preserveAttachmentCount = 0,
        .pPreserveAttachments = nullptr
    };

    const auto subpass_dependency = VkSubpassDependency{
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dependencyFlags = 0
    };

    const auto render_pass_create_info = VkRenderPassCreateInfo{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .attachmentCount = 1,
        .pAttachments = &color_attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &subpass_dependency
    };

    auto render_pass = VkRenderPass{};
    VK_CHECK(vkCreateRenderPass(logical_device, &render_pass_create_info, nullptr, &render_pass));

    const auto dynamic_states = std::array{
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    const auto dynamic_state = VkPipelineDynamicStateCreateInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .dynamicStateCount = static_cast<uint32_t>(dynamic_states.size()),
        .pDynamicStates = dynamic_states.data()
    };

    const auto pipeline_create_info = VkGraphicsPipelineCreateInfo{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stageCount = shader_stage_create_infos.size(),
        .pStages = shader_stage_create_infos.data(),
        .pVertexInputState = &vertex_input_create_info,
        .pInputAssemblyState = &input_assembly_create_info,
        .pTessellationState = nullptr,
        .pViewportState = &viewport_state_create_info,
        .pRasterizationState = &rasterization_state_create_info,
        .pMultisampleState = &multisample_state_create_info,
        .pDepthStencilState = nullptr,
        .pColorBlendState = &color_blend_state_create_info,
        .pDynamicState = &dynamic_state,
        .layout = pipeline_layout,
        .renderPass = render_pass,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = 0
    };

    auto pipeline = VkPipeline{ 0 };
    vkCreateGraphicsPipelines(logical_device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &pipeline);

    vkDestroyShaderModule(logical_device, triangle_vertex_shader, nullptr);
    vkDestroyShaderModule(logical_device, triangle_fragment_shader, nullptr);

    return std::tuple{pipeline, pipeline_layout, render_pass};
}

auto create_swapchain_framebuffers(VkDevice logical_device, VkRenderPass render_pass, const std::vector<VkImageView> swapchain_imageviews, VkExtent2D swapchain_extent)
{
    auto framebuffers = std::vector<VkFramebuffer>(swapchain_imageviews.size());

    for (int i = 0; const auto& image_view : swapchain_imageviews)
    {
        const auto create_info = VkFramebufferCreateInfo{
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .renderPass = render_pass,
            .attachmentCount = 1,
            .pAttachments = &image_view,
            .width = swapchain_extent.width,
            .height = swapchain_extent.height,
            .layers = 1
        };

        VK_CHECK(vkCreateFramebuffer(logical_device, &create_info, nullptr, &framebuffers[i]));
    }

    return framebuffers;
}

auto create_command_pool(VkDevice logical_device, uint32_t queue_family_index)
{
    const auto create_info = VkCommandPoolCreateInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queue_family_index
    };

    auto command_pool = VkCommandPool{};
    VK_CHECK(vkCreateCommandPool(logical_device, &create_info, nullptr, &command_pool));

    return command_pool;
}

auto create_command_buffers(VkDevice logical_device, VkCommandPool command_pool, uint32_t count)
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

    return command_buffers;
}

auto create_semaphores(VkDevice logical_device, uint32_t count)
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

        semaphores[i] = semaphore;
    }

    return semaphores;
}

auto create_fences(VkDevice logical_device, uint32_t count)
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

        fences[i] = fence;
    }

    return fences;
}

auto recreate_swapchain(GLFWwindow* window, VkDevice logical_device, VkPhysicalDevice physical_device, VkSurfaceKHR surface, uint32_t queue_family_index, VkFormat swapchain_format)
{
    int glfw_fb_extent_width, glfw_fb_extent_height;
    glfwGetFramebufferSize(window, &glfw_fb_extent_width, &glfw_fb_extent_height);
    spdlog::debug("GLFW framebuffer size: ({}, {})", glfw_fb_extent_width, glfw_fb_extent_height);
    const auto glfw_extent = VkExtent2D{ static_cast<uint32_t>(glfw_fb_extent_width), static_cast<uint32_t>(glfw_fb_extent_height) };

    const auto [swapchain, surface_format] = create_swapchain(logical_device, physical_device, surface, queue_family_index, glfw_extent);
    const auto [swapchain_images, swapchain_image_views] = get_swapchain_images(logical_device, swapchain, surface_format.format);

    const auto color_attachment = VkAttachmentDescription{
        .flags = 0,
        .format = swapchain_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };

    const auto attachmentRef = VkAttachmentReference{
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };

    const auto subpass = VkSubpassDescription{
        .flags = 0,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .inputAttachmentCount = 0,
        .pInputAttachments = nullptr,
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachmentRef,
        .pResolveAttachments = 0,
        .pDepthStencilAttachment = nullptr,
        .preserveAttachmentCount = 0,
        .pPreserveAttachments = nullptr
    };

    const auto subpass_dependency = VkSubpassDependency{
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dependencyFlags = 0
    };

    const auto render_pass_create_info = VkRenderPassCreateInfo{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .attachmentCount = 1,
        .pAttachments = &color_attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &subpass_dependency
    };

    auto render_pass = VkRenderPass{};
    VK_CHECK(vkCreateRenderPass(logical_device, &render_pass_create_info, nullptr, &render_pass));

    const auto swapchain_framebuffers = create_swapchain_framebuffers(logical_device, render_pass, swapchain_image_views, glfw_extent);

    return std::tuple{glfw_extent, swapchain, surface_format, swapchain_images, swapchain_image_views, render_pass, swapchain_framebuffers};
}

void handle_keyboard(GLFWwindow* window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE))
    {
        glfwSetWindowShouldClose(window, true);
    }
}

auto create_buffer(VkDevice logical_device, uint32_t queue_family_index, std::size_t size)
{
    const auto create_info = VkBufferCreateInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = size,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 1,
        .pQueueFamilyIndices = &queue_family_index
    };

    auto buffer = VkBuffer{};
    VK_CHECK(vkCreateBuffer(logical_device, &create_info, nullptr, &buffer));

    auto memory_requirements = VkMemoryRequirements{};
    vkGetBufferMemoryRequirements(logical_device, buffer, &memory_requirements);

    return std::tuple{buffer, memory_requirements};
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

auto allocate_buffer(VkDevice logical_device, std::size_t size, uint32_t memory_type_index)
{
    const auto allocate_info = VkMemoryAllocateInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = size,
        .memoryTypeIndex = memory_type_index
    };

    auto memory = VkDeviceMemory{};
    vkAllocateMemory(logical_device, &allocate_info, nullptr, &memory);

    return memory;
}

auto map_memory(VkDevice logical_device, VkDeviceMemory device_memory, const void* in_data, std::size_t size)
{
    void* pData;
    VK_CHECK(vkMapMemory(logical_device, device_memory, 0, size, 0, &pData));

    std::memcpy(pData, in_data, size);

    vkUnmapMemory(logical_device, device_memory);
}

int main()
{
    struct Vertex
    {
        glm::vec3 pos;
        glm::vec3 color;
    };

    auto triangle_vertex_buffer = std::vector<Vertex>{
        Vertex{ glm::vec3{ 0.f, -0.5f, 0.f }, glm::vec3{ 1.f, 0.f, 0.f } },
        Vertex{ glm::vec3{ 0.5, 0.5, 0.f }, glm::vec3{ 0.f, 1.f, 0.f } },
        Vertex{ glm::vec3{ -0.5, 0.5, 0.f }, glm::vec3{ 0.f, 0.f, 1.f } }
    };

    const auto triangle_vertex_buffer_size = triangle_vertex_buffer.size() * sizeof(Vertex);

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
    auto glfw_extent = VkExtent2D{ static_cast<uint32_t>(glfw_fb_extent_width), static_cast<uint32_t>(glfw_fb_extent_height) };
    auto [swapchain, surface_format] = create_swapchain(logical_device, physical_device, surface, queue_family_index, glfw_extent);
    auto [swapchain_images, swapchain_image_views] = get_swapchain_images(logical_device, swapchain, surface_format.format);

    auto [pipeline, pipeline_layout, render_pass] = create_graphics_pipeline(logical_device, glfw_extent, surface_format.format);
    auto swapchain_framebuffers = create_swapchain_framebuffers(logical_device, render_pass, swapchain_image_views, glfw_extent);
    const auto command_pool = create_command_pool(logical_device, queue_family_index);

    constexpr auto max_frames_in_flight = 2;
    const auto command_buffers = create_command_buffers(logical_device, command_pool, max_frames_in_flight);

    const auto& [vertex_buffer, vertex_buffer_memory_requirements] = create_buffer(logical_device, queue_family_index, triangle_vertex_buffer_size);
    const auto memory_type_index = find_memory_type_index(physical_device, vertex_buffer_memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    const auto device_memory = allocate_buffer(logical_device, triangle_vertex_buffer_size, memory_type_index);
    VK_CHECK(vkBindBufferMemory(logical_device, vertex_buffer, device_memory, 0));
    map_memory(logical_device, device_memory, triangle_vertex_buffer.data(), triangle_vertex_buffer_size);

    const auto image_available_semaphores = create_semaphores(logical_device, max_frames_in_flight);
    const auto rendering_finished_semaphores = create_semaphores(logical_device, max_frames_in_flight);
    const auto in_flight_fences = create_fences(logical_device, max_frames_in_flight);

    spdlog::trace("Entering main loop.");
    auto current_frame = uint32_t{ 0 };
    auto image_index = uint32_t{ 0 };
    while (!glfwWindowShouldClose(window))
    {
        handle_keyboard(window);

        const auto in_flight_fence = in_flight_fences[current_frame];
        const auto image_available_semaphore = image_available_semaphores[current_frame];
        const auto rendering_finished_semaphore = rendering_finished_semaphores[current_frame];
        const auto command_buffer = command_buffers[current_frame];

        glfwPollEvents();

        VK_CHECK(vkWaitForFences(logical_device, 1, &in_flight_fence, VK_TRUE, UINT64_MAX));

        {
            const auto result = vkAcquireNextImageKHR(logical_device, swapchain, UINT64_MAX, image_available_semaphore, VK_NULL_HANDLE, &image_index);
            if (result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR)
            {
                spdlog::info("Swapchain images no longer match native surface properties. Recreating swapchain.");
                VK_CHECK(vkDeviceWaitIdle(logical_device));
                for (const auto& fb : swapchain_framebuffers)
                {
                    vkDestroyFramebuffer(logical_device, fb, nullptr);
                }
                for (const auto iv : swapchain_image_views)
                {
                    vkDestroyImageView(logical_device, iv, nullptr);
                }
                vkDestroyRenderPass(logical_device, render_pass, nullptr);
                std::tie(glfw_extent, swapchain, surface_format, swapchain_images, swapchain_image_views, render_pass, swapchain_framebuffers) = recreate_swapchain(window, logical_device, physical_device, surface, queue_family_index, surface_format.format);
                continue;
            }
            else if (result != VK_SUCCESS)
            {
                throw std::runtime_error("");
            }
        }

        VK_CHECK(vkResetFences(logical_device, 1, &in_flight_fence));

        const auto begin_info = VkCommandBufferBeginInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr
        };

        VK_CHECK(vkResetCommandBuffer(command_buffer, 0));
        VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));

        const auto clear_color = VkClearValue{
            .color = VkClearColorValue{
                .float32 = {130.f / 255.f, 163.f / 255.f, 255.f / 255.f}
            }
        };
        
        const auto render_pass_begin_info = VkRenderPassBeginInfo{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext = nullptr,
            .renderPass = render_pass,
            .framebuffer = swapchain_framebuffers[image_index],
            .renderArea = VkRect2D {
                .offset = VkOffset2D { 0, 0 },
                .extent = glfw_extent
            },
            .clearValueCount = 1,
            .pClearValues = &clear_color
        };

        vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

        const auto viewport = VkViewport{
            .x = 0.f,
            .y = 0.f,
            .width = static_cast<float>(glfw_extent.width),
            .height = static_cast<float>(glfw_extent.height),
            .minDepth = 0.f,
            .maxDepth = 1.f
        };

        const auto scissors = VkRect2D{
            .offset = VkOffset2D{
                .x = 0,
                .y = 0,
            },
            .extent = glfw_extent
        };

        vkCmdSetViewport(command_buffer, 0, 1, &viewport);
        vkCmdSetScissor(command_buffer, 0, 1, &scissors);

        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        const auto offsets = std::array<VkDeviceSize, 1>{ 0 };
        vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer, offsets.data());
        vkCmdDraw(command_buffer, 3, 1, 0, 0);

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

        vkCmdEndRenderPass(command_buffer);
        VK_CHECK(vkEndCommandBuffer(command_buffer));

        VK_CHECK(vkQueueSubmit(present_queue, 1, &submit_info, in_flight_fence));

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

        current_frame = (current_frame + 1) % max_frames_in_flight;
    }

    VK_CHECK(vkDeviceWaitIdle(logical_device));

    spdlog::trace("Cleanup.");
    vkFreeMemory(logical_device, device_memory, nullptr);
    vkDestroyBuffer(logical_device, vertex_buffer, nullptr);
    for (const auto fence : in_flight_fences)
    {
        vkDestroyFence(logical_device, fence, nullptr);
    }
    for (const auto sem : rendering_finished_semaphores)
    {
        vkDestroySemaphore(logical_device, sem, nullptr);
    }
    for (const auto sem : image_available_semaphores)
    {
        vkDestroySemaphore(logical_device, sem, nullptr);
    }
    vkDestroyCommandPool(logical_device, command_pool, nullptr);
    for (const auto& fb : swapchain_framebuffers)
    {
        vkDestroyFramebuffer(logical_device, fb, nullptr);
    }
    for (const auto iv : swapchain_image_views)
    {
        vkDestroyImageView(logical_device, iv, nullptr);
    }
    vkDestroyPipelineLayout(logical_device, pipeline_layout, nullptr);
    vkDestroyPipeline(logical_device, pipeline, nullptr);
    vkDestroyRenderPass(logical_device, render_pass, nullptr);
    vkDestroySwapchainKHR(logical_device, swapchain, nullptr);
    vkDestroySurfaceKHR(vk_instance, surface, nullptr);
    vkDestroyDevice(logical_device, nullptr);
    vkDestroyDebugUtilsMessengerEXT(vk_instance, debug_messenger, nullptr);
    vkDestroyInstance(vk_instance, nullptr);

    glfwDestroyWindow(window);
    glfwTerminate();
}
