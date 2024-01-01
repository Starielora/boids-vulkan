#include "camera.hpp"
#include "cleanup.hpp"
#include "setup.hpp"
#include "vkcheck.hpp"
#include "boids.hpp"
#include "light.hpp"
#include "cone.hpp"
#include "aquarium.hpp"
#include "constants.hpp"
#include "grid.hpp"
#include "gui.hpp"
#include "shader_module_cache.hpp"
#include "constants.hpp"
#include "boids_update.hpp"

#include <glm/glm.hpp>
#include <spdlog/spdlog.h>
#include <shaders/shaders.h>

#include <vector>
#include <array>
#include <numeric>

constexpr bool VALIDATION_LAYERS = true;

camera g_camera;
bool g_gui_mode = false;
glm::vec2 gui_mode_mouse_pos{};
const auto flip_clip_space = glm::scale(glm::mat4(1.), glm::vec3(1, -1, 1));

auto general_queue = cleanup::queue_type{};
auto swapchain_queue = cleanup::queue_type{};

auto visual_range = 1.f;
auto cohesion_weight = 0.025f;
auto separation_weight = 0.005f;
auto alignment_weight = 1.00f;
auto wall_force_weight = 0.1f;

auto model_speed = 0.1f;
auto model_scale = glm::vec3(0.15, 0.15, 0.15);

auto fps = std::vector<float>(100);
auto fps_index = 0u;

constexpr auto instances_count = 1000;

namespace aquarium
{
    constexpr float scale = 30.f;
    const auto min_range = glm::vec4(-scale, 0.f, -scale, 0.f);
    const auto max_range = glm::vec4(scale, scale, scale, 0.f);

    const auto wall_repellents = get_wall_repellents(min_range, max_range, wall_force_weight);
}

namespace optimization
{
    constexpr auto grid_cells_count = glm::uvec4(3, 3, 3, 0);

    auto create_boids_to_cells_pipeline(VkDevice logical_device, VkDescriptorSetLayout layout, shaders::module_cache& shaders_cache, cleanup::queue_type& cleanup_queue)
    {
        const auto push_constant_range = VkPushConstantRange{
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .offset = 0,
            .size = sizeof(glm::vec4) + sizeof(glm::vec4) + sizeof(glm::uvec4)
        };

        const auto pipeline_layout_create_info = VkPipelineLayoutCreateInfo{
             .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
             .pNext = nullptr,
             .flags = {},
             .setLayoutCount = 1,
             .pSetLayouts = &layout,
             .pushConstantRangeCount = 1,
             .pPushConstantRanges = &push_constant_range
        };

        auto pipeline_layout = VkPipelineLayout{};
        VK_CHECK(vkCreatePipelineLayout(logical_device, &pipeline_layout_create_info, nullptr, &pipeline_layout));

        cleanup_queue.push([logical_device, pipeline_layout] {
            vkDestroyPipelineLayout(logical_device, pipeline_layout, nullptr);
        });

        static const auto create_info = VkComputePipelineCreateInfo{
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .pNext = nullptr,
            .flags = {},
            .stage = VkPipelineShaderStageCreateInfo{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = {},
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = shaders_cache.get_module(shader_path::compute::boids_to_cells),
                .pName = "main",
                .pSpecializationInfo = nullptr,
            },
            .layout = pipeline_layout,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = 0
        };

        auto pipeline = VkPipeline{};
        VK_CHECK(vkCreateComputePipelines(logical_device, VK_NULL_HANDLE, 1, &create_info, nullptr, &pipeline));

        cleanup_queue.push([logical_device, pipeline](){
            vkDestroyPipeline(logical_device, pipeline, nullptr);
        });

        return std::tuple{ pipeline, pipeline_layout };
    }

    auto create_link_boids_in_cells_pipeline(VkDevice logical_device, VkDescriptorSetLayout layout, shaders::module_cache& shaders_cache, cleanup::queue_type& cleanup_queue)
    {
        const auto push_constant_range = VkPushConstantRange{
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .offset = 0,
            .size = sizeof(glm::uvec4)
        };

        const auto pipeline_layout_create_info = VkPipelineLayoutCreateInfo{
             .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
             .pNext = nullptr,
             .flags = {},
             .setLayoutCount = 1,
             .pSetLayouts = &layout,
             .pushConstantRangeCount = 1,
             .pPushConstantRanges = &push_constant_range
        };

        auto pipeline_layout = VkPipelineLayout{};
        VK_CHECK(vkCreatePipelineLayout(logical_device, &pipeline_layout_create_info, nullptr, &pipeline_layout));

        cleanup_queue.push([logical_device, pipeline_layout] {
            vkDestroyPipelineLayout(logical_device, pipeline_layout, nullptr);
        });

        static const auto create_info = VkComputePipelineCreateInfo{
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .pNext = nullptr,
            .flags = {},
            .stage = VkPipelineShaderStageCreateInfo{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = {},
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = shaders_cache.get_module(shader_path::compute::link_boids_in_cells),
                .pName = "main",
                .pSpecializationInfo = nullptr,
            },
            .layout = pipeline_layout,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = 0
        };

        auto pipeline = VkPipeline{};
        VK_CHECK(vkCreateComputePipelines(logical_device, VK_NULL_HANDLE, 1, &create_info, nullptr, &pipeline));

        cleanup_queue.push([logical_device, pipeline](){
            vkDestroyPipeline(logical_device, pipeline, nullptr);
        });

        return std::tuple{ pipeline, pipeline_layout };
    }
}


struct lights_data
{
    std::vector<directional_light> dir_lights;
    std::vector<point_light> point_lights;
};

auto lights = lights_data{
    .dir_lights = std::vector{
        directional_light{
            .direction = { 0, -1, 0, 0 },
            .ambient = { 0.5, 0.5, 0.5, 0 },
            .diffuse = { 0.5, 0.5, 0.5, 0 },
            .specular = { 1., 1., 1., 0 }
        },
    },
    .point_lights = std::vector<point_light>{
        point_light{
            .position = { 15, 15, 0, 0 },
            .ambient = { 0, 1, 0, 1 },
            .diffuse = { 0, 1, 0, 1 },
            .specular = { 0, 1, 0, 1 },
            .constant = 1.f,
            .linear = 0.09f,
            .quadratic = 0.032f
        },
        point_light{
            .position = { -15, 15, 0, 0 },
            .ambient = { 0, 0, 1, 1 },
            .diffuse = { 0, 0, 1, 1 },
            .specular = { 0, 0, 1, 1 },
            .constant = 1.f,
            .linear = 0.09f,
            .quadratic = 0.032f
        },
    }
};

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
            glfwSetCursorPos(window, w / 2.f, h / 2.f);
        }
        else
        {
            glfwSetCursorPos(window, gui_mode_mouse_pos.x, gui_mode_mouse_pos.y);
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }

    }
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

auto recreate_graphics_pipeline_and_swapchain(GLFWwindow* window, VkDevice logical_device, VkPhysicalDevice physical_device, shaders::module_cache& shaders_cache, VkPipelineLayout pipeline_layout, VkRenderPass render_pass, VkSurfaceKHR surface, uint32_t queue_family_index, VkFormat swapchain_format, cleanup::queue_type& cleanup_queue)
{
    const auto window_extent = window::get_extent(window);
    spdlog::info("New extent: {}, {}", window_extent.width, window_extent.height);

    auto graphics_pipelines = create_graphics_pipelines(logical_device, {
        cone::get_pipeline_create_info(logical_device, pipeline_layout, render_pass, window_extent, shaders_cache),
        grid::get_pipeline_create_info(logical_device, pipeline_layout, render_pass, window_extent, shaders_cache),
        aquarium::get_pipeline_create_info(logical_device, pipeline_layout, render_pass, window_extent, shaders_cache),
        light::get_pipeline_create_info(logical_device, pipeline_layout, render_pass, window_extent, shaders_cache),
    }, cleanup_queue);
    const auto& [swapchain, surface_format] = create_swapchain(logical_device, physical_device, surface, queue_family_index, window_extent, cleanup_queue);
    const auto& [swapchain_images, swapchain_image_views] = get_swapchain_images(logical_device, swapchain, surface_format.format, cleanup_queue);

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

    const auto window = window::create(general_queue, mouse_callback, key_callback);

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

    const auto vk_instance = create_vulkan_instance(requested_instance_layers, requested_extensions, general_queue);
    volkLoadInstance(vk_instance);

    create_debug_utils_messenger(vk_instance, general_queue);

    const auto surface = window::create_vk_surface(vk_instance, window, general_queue);

    const auto& [physical_device, queue_family_index, physical_device_properties] = pick_physical_device(vk_instance, surface, required_device_extensions);
    const auto& [logical_device, present_graphics_compute_queue] = create_logical_device(physical_device, queue_family_index, required_device_extensions, general_queue);

    auto window_extent = window::get_extent(window);

    auto [swapchain, surface_format] = create_swapchain(logical_device, physical_device, surface, queue_family_index, window_extent, swapchain_queue);
    auto [swapchain_images, swapchain_image_views] = get_swapchain_images(logical_device, swapchain, surface_format.format, swapchain_queue);

    const auto render_pass = create_render_pass(logical_device, surface_format.format, depth_format, msaa_samples, general_queue);
    const auto descriptor_set_layout = create_descriptor_sets_layouts(logical_device, general_queue);
    const auto graphics_pipeline_layout = create_pipeline_layout(logical_device, { descriptor_set_layout }, general_queue);
    const auto compute_pipeline_layout = boids_update::get_pipeline_layout(logical_device, descriptor_set_layout, general_queue);

    auto shader_cache = shaders::module_cache(logical_device);
    general_queue.push([&shader_cache]() { shader_cache.clear(); });

    auto graphics_pipelines = create_graphics_pipelines(logical_device, {
        cone::get_pipeline_create_info(logical_device, graphics_pipeline_layout, render_pass, window_extent, shader_cache),
        grid::get_pipeline_create_info(logical_device, graphics_pipeline_layout, render_pass, window_extent, shader_cache),
        aquarium::get_pipeline_create_info(logical_device, graphics_pipeline_layout, render_pass, window_extent, shader_cache),
        light::get_pipeline_create_info(logical_device, graphics_pipeline_layout, render_pass, window_extent, shader_cache),
    }, swapchain_queue);

    const auto boids_compute_pipeline = create_boids_update_compute_pipeline(logical_device, boids_update::get_pipeline_create_info(logical_device, compute_pipeline_layout, shader_cache), general_queue);
    auto [boids_to_cells_pipeline, boids_to_cells_pipeline_layout] = optimization::create_boids_to_cells_pipeline(logical_device, descriptor_set_layout, shader_cache, general_queue);
    auto [link_boids_pipeline, link_boids_pipeline_layout] = optimization::create_link_boids_in_cells_pipeline(logical_device, descriptor_set_layout, shader_cache, general_queue);

    auto& cone_pipeline = graphics_pipelines[0];
    auto& grid_pipeline = graphics_pipelines[1];
    auto& aquarium_pipeline = graphics_pipelines[2];
    auto& debug_cube_pipeilne = graphics_pipelines[3];

    constexpr auto overlapping_frames_count = 2;

    const auto descriptor_pool = create_descriptor_pool(logical_device,  general_queue);
    const auto descriptor_sets = allocate_descriptor_sets(logical_device, { descriptor_set_layout }, descriptor_pool, overlapping_frames_count);
    const auto descriptor_update_template = create_descriptor_update_template(logical_device, descriptor_set_layout, graphics_pipeline_layout, general_queue);

    struct
    {
        glm::vec4 position;
        glm::mat4 viewproj;
    } camera_data;

    auto model_data = std::vector<boids::boid>(instances_count);

    auto model_data_span = std::span(model_data.data(), model_data.data() + instances_count);
    cone::generate_model_data(model_data_span, aquarium::min_range, aquarium::max_range);

    const auto camera_data_padded_size = pad_uniform_buffer_size(sizeof(camera_data), physical_device_properties.limits.minUniformBufferOffsetAlignment);
    const auto& [camera_data_buffer, camera_data_memory] = create_buffer(logical_device, physical_device, overlapping_frames_count * camera_data_padded_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, general_queue);
    void* camera_data_memory_ptr = nullptr;
    VK_CHECK(vkMapMemory(logical_device, camera_data_memory, 0, VK_WHOLE_SIZE, 0, &camera_data_memory_ptr));
    const auto camera_data_descriptor_buffer_infos = get_descriptor_buffer_infos(camera_data_buffer, camera_data_padded_size, overlapping_frames_count);

    const auto model_data_padded_size = pad_uniform_buffer_size(model_data.size() * sizeof(decltype(model_data)::value_type), physical_device_properties.limits.minUniformBufferOffsetAlignment);
    const auto& [model_data_buffer, model_data_memory] = create_buffer(logical_device, physical_device, overlapping_frames_count * model_data_padded_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, general_queue);
    void* model_data_memory_ptr = nullptr;
    VK_CHECK(vkMapMemory(logical_device, model_data_memory, 0, VK_WHOLE_SIZE, 0, &model_data_memory_ptr));
    const auto model_data_descriptor_buffer_infos = get_descriptor_buffer_infos(model_data_buffer, model_data_padded_size, overlapping_frames_count);

    const auto dir_lights_data_padded_size = pad_uniform_buffer_size(lights.dir_lights.size() * sizeof(decltype(lights.dir_lights)::value_type), physical_device_properties.limits.minUniformBufferOffsetAlignment);
    const auto& [dir_lights_data_buffer, dir_lights_data_memory] = create_buffer(logical_device, physical_device, overlapping_frames_count * dir_lights_data_padded_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, general_queue);
    void* dir_lights_data_memory_ptr = nullptr;
    VK_CHECK(vkMapMemory(logical_device, dir_lights_data_memory, 0, VK_WHOLE_SIZE, 0, &dir_lights_data_memory_ptr));
    const auto dir_lights_data_descriptor_buffer_infos = get_descriptor_buffer_infos(dir_lights_data_buffer, dir_lights_data_padded_size, overlapping_frames_count);

    const auto point_lights_data_padded_size = pad_uniform_buffer_size(lights.point_lights.size() * sizeof(decltype(lights.point_lights)::value_type), physical_device_properties.limits.minUniformBufferOffsetAlignment);
    const auto& [point_lights_data_buffer, point_lights_data_memory] = create_buffer(logical_device, physical_device, overlapping_frames_count * point_lights_data_padded_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, general_queue);
    void* point_lights_data_memory_ptr = nullptr;
    VK_CHECK(vkMapMemory(logical_device, point_lights_data_memory, 0, VK_WHOLE_SIZE, 0, &point_lights_data_memory_ptr));
    const auto point_lights_data_descriptor_buffer_infos = get_descriptor_buffer_infos(point_lights_data_buffer, point_lights_data_padded_size, overlapping_frames_count);

    auto [color_image, color_image_view, color_image_memory] = create_color_image(logical_device, physical_device, surface_format.format, window_extent, swapchain_queue);
    auto [depth_image, depth_image_view, depth_image_memory] = create_depth_image(logical_device, physical_device, window_extent, swapchain_queue);

    auto swapchain_framebuffers = create_swapchain_framebuffers(logical_device, render_pass, { color_image_view }, swapchain_image_views, { depth_image_view }, window_extent, swapchain_queue);

    const auto command_pool = create_command_pool(logical_device, queue_family_index, general_queue);
    const auto command_buffers = create_command_buffers(logical_device, command_pool, overlapping_frames_count, general_queue);

    constexpr auto cells_buffer_size = instances_count * optimization::grid_cells_count.x * optimization::grid_cells_count.y * optimization::grid_cells_count.z * sizeof(glm::uvec4);
    const auto cells_buffer_padded_size = pad_uniform_buffer_size(cells_buffer_size, physical_device_properties.limits.minUniformBufferOffsetAlignment);
    const auto& [cells_buffer, cells_memory] = create_buffer(logical_device, physical_device, overlapping_frames_count * cells_buffer_padded_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, general_queue);
    const auto cells_buffer_descriptor_buffer_infos = get_descriptor_buffer_infos(cells_buffer, cells_buffer_padded_size, overlapping_frames_count);

    const auto cone_vertex_buffer = cone::generate_vertex_data();
    const auto cone_vertex_buffer_size = cone_vertex_buffer.size() * sizeof(vertex);
    //const auto cone_index_buffer_size = cone_index_buffer.size() * sizeof(decltype(cone_index_buffer)::value_type);

    const auto& [vertex_buffer, device_memory] = create_buffer(logical_device, physical_device, cone_vertex_buffer_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, general_queue);
    copy_memory(logical_device, device_memory, 0, cone_vertex_buffer.data(), cone_vertex_buffer_size);
    //copy_memory(logical_device, device_memory, cone_vertex_buffer_size, cone_index_buffer.data(), cone_index_buffer_size);

    const auto image_available_semaphores = create_semaphores(logical_device, overlapping_frames_count, general_queue);
    const auto rendering_finished_semaphores = create_semaphores(logical_device, overlapping_frames_count, general_queue);
    const auto overlapping_frames_fences = create_fences(logical_device, overlapping_frames_count, general_queue);

    gui::init(window, vk_instance, logical_device, physical_device, queue_family_index, present_graphics_compute_queue, overlapping_frames_count, render_pass, surface, surface_format, swapchain, command_pool, command_buffers[0], general_queue);

    auto gui_data = gui::data_refs{
        .model_scale = model_scale.x,
        .model_speed = model_speed,
        .camera = g_camera,
        .cohesion_weight = cohesion_weight,
        .separation_weight = separation_weight,
        .alignment_weight = alignment_weight,
        .visual_range = visual_range,
        .wall_force_weight = wall_force_weight,
        .cones = model_data_span,
        .dir_lights = lights.dir_lights,
        .point_lights = lights.point_lights,
        .fps = fps,
    };

    std::memcpy(reinterpret_cast<char*>(model_data_memory_ptr) + 0 * model_data_padded_size, model_data.data(), model_data.size() * sizeof(decltype(model_data)::value_type));
    std::memcpy(reinterpret_cast<char*>(model_data_memory_ptr) + 1 * model_data_padded_size, model_data.data(), model_data.size() * sizeof(decltype(model_data)::value_type));

    spdlog::trace("Entering main loop.");
    auto current_frame = uint32_t{ 0 };
    auto image_index = uint32_t{ 0 };
    while (!glfwWindowShouldClose(window))
    {
        const auto t1 = std::chrono::steady_clock::now();
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
                cleanup::flush(swapchain_queue);

                std::tie(graphics_pipelines, window_extent, swapchain, surface_format, swapchain_images, swapchain_image_views, swapchain_framebuffers, color_image, color_image_memory, color_image_view, depth_image, depth_image_view, depth_image_memory) = recreate_graphics_pipeline_and_swapchain(window, logical_device, physical_device, shader_cache, graphics_pipeline_layout, render_pass, surface, queue_family_index, surface_format.format, swapchain_queue);
                cone_pipeline = graphics_pipelines[0];
                grid_pipeline = graphics_pipelines[1];
                aquarium_pipeline = graphics_pipelines[2];
                debug_cube_pipeilne = graphics_pipelines[3];
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

        // update camera
        camera_data.position = glm::vec4(g_camera.position(), 0.f);
        camera_data.viewproj = flip_clip_space * g_camera.projection(window_extent.width, window_extent.height) * g_camera.view();
        std::memcpy(reinterpret_cast<char*>(camera_data_memory_ptr) + current_frame * camera_data_padded_size, &camera_data, sizeof(camera_data));

        // update lights
        std::memcpy(reinterpret_cast<char*>(dir_lights_data_memory_ptr) + current_frame * dir_lights_data_padded_size, lights.dir_lights.data(), lights.dir_lights.size() * sizeof(decltype(lights.dir_lights)::value_type));
        std::memcpy(reinterpret_cast<char*>(point_lights_data_memory_ptr) + current_frame * point_lights_data_padded_size, lights.point_lights.data(), lights.point_lights.size() * sizeof(decltype(lights.point_lights)::value_type));

        // TODO performance :(
        {
            //std::memcpy(model_data.data(), (reinterpret_cast<char*>(model_data_memory_ptr) + current_frame * model_data_padded_size), model_data_padded_size);
        }

        const auto buffer_infos = std::array{
            camera_data_descriptor_buffer_infos[current_frame],
            model_data_descriptor_buffer_infos[current_frame],
            dir_lights_data_descriptor_buffer_infos[current_frame],
            point_lights_data_descriptor_buffer_infos[current_frame],
            model_data_descriptor_buffer_infos[(current_frame + 1) % overlapping_frames_count],
            cells_buffer_descriptor_buffer_infos[current_frame],
        };

        vkUpdateDescriptorSetWithTemplate(logical_device, descriptor_sets[current_frame], descriptor_update_template, buffer_infos.data());

        vkCmdFillBuffer(command_buffer, cells_buffer, current_frame * cells_buffer_padded_size, cells_buffer_padded_size, 0);
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, boids_to_cells_pipeline);
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, boids_to_cells_pipeline_layout, 0, 1, &descriptor_sets[current_frame], 0, nullptr);
        vkCmdPushConstants(command_buffer, boids_to_cells_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(aquarium::max_range), &aquarium::max_range[0]);
        vkCmdPushConstants(command_buffer, boids_to_cells_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, sizeof(aquarium::max_range), sizeof(aquarium::min_range), &aquarium::min_range[0]);
        vkCmdPushConstants(command_buffer, boids_to_cells_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, sizeof(aquarium::max_range) + sizeof(aquarium::min_range), sizeof(optimization::grid_cells_count), &optimization::grid_cells_count[0]);
        vkCmdDispatch(command_buffer, instances_count, 1, 1);

        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, link_boids_pipeline);
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, link_boids_pipeline_layout, 0, 1, &descriptor_sets[current_frame], 0, nullptr);
        vkCmdPushConstants(command_buffer, boids_to_cells_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(optimization::grid_cells_count), &optimization::grid_cells_count[0]);
        vkCmdDispatch(command_buffer, optimization::grid_cells_count.x, optimization::grid_cells_count.y, optimization::grid_cells_count.z);

        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, boids_compute_pipeline);
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline_layout, 0, 1, &descriptor_sets[current_frame], 0, nullptr);
        const auto compute_push_constants = std::array<float, 8>{ gui_data.model_scale, gui_data.model_speed, aquarium::scale, gui_data.visual_range, gui_data.cohesion_weight, gui_data.separation_weight, gui_data.alignment_weight, 0.f };
        vkCmdPushConstants(command_buffer, compute_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(decltype(compute_push_constants)::value_type) * compute_push_constants.size(), compute_push_constants.data());
        vkCmdDispatch(command_buffer, instances_count, 1, 1);

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

        vkCmdPushConstants(command_buffer, graphics_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float), &aquarium::scale);
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline_layout, 0, 1, &descriptor_sets[current_frame], 0, nullptr);
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, cone_pipeline);
        const auto offsets = std::array{ VkDeviceSize{ 0 } };
        vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer, offsets.data());
        vkCmdDraw(command_buffer, cone_vertex_buffer.size(), instances_count, 0, 0);

        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, debug_cube_pipeilne);
        vkCmdDraw(command_buffer, 36, lights.point_lights.size(), 0, 0);

        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, aquarium_pipeline);
        vkCmdDraw(command_buffer, 36, 1, 0, 0);

        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, grid_pipeline);
        vkCmdDraw(command_buffer, 6, 1, 0, 0);

        gui::draw(command_buffer, gui_data);

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

        VK_CHECK(vkQueueSubmit(present_graphics_compute_queue, 1, &submit_info, fence));

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

        VK_CHECK(vkQueuePresentKHR(present_graphics_compute_queue, &present_info));

        current_frame = (current_frame + 1) % overlapping_frames_count;

        const auto t2 = std::chrono::steady_clock::now();
        fps[fps_index++ % fps.size()] = 1000.f / std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
        const auto avg = std::accumulate(fps.begin(), fps.end(), 0) / fps.size();
        glfwSetWindowTitle(window, fmt::format("boids ({} fps)", avg).c_str());
    }

    VK_CHECK(vkDeviceWaitIdle(logical_device));

    spdlog::trace("Cleanup.");

    cleanup::flush(swapchain_queue);
    cleanup::flush(general_queue);
}
