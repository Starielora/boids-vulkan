#pragma once

#include "cleanup.hpp"
#include "camera.hpp"
#include "light.hpp"
#include "boids.hpp"

#include <Volk/volk.h>
#include <GLFW/glfw3.h>

#include <array>
#include <span>

namespace gui
{
    struct data_refs
    {
        float& model_scale;
        float& model_speed;
        camera& camera;
        float& cohesion_weight;
        float& separation_weight;
        float& alignment_weight;
        float& visual_range;
        float& wall_force_weight;
        std::span<boids::boid>& cones;
        std::vector<directional_light>& dir_lights;
        std::vector<point_light>& point_lights;
        std::vector<float>& fps;
    };

    VkDescriptorPool create_descriptor_pool(VkDevice logical_device, cleanup::queue_type& cleanup_queue);
    void init(GLFWwindow* window, VkInstance vk_instance, VkDevice logical_device, VkPhysicalDevice physical_device, uint32_t queue_family_index, VkQueue queue, uint32_t images_count, VkRenderPass render_pass, VkSurfaceKHR surface, VkSurfaceFormatKHR surface_format, VkSwapchainKHR swapchain, VkCommandPool command_pool, VkCommandBuffer command_buffer, cleanup::queue_type& cleanup_queue);
    void draw(VkCommandBuffer command_buffer, data_refs& data_refs);
}
