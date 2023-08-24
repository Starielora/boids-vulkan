#pragma once

#include "boids.hpp" // huh?
#include "constants.hpp" // huh?

#define VK_NO_PROTOTYPES
#include <Volk/volk.h>
#include <array>
#include <random>
#include <span>

namespace cone
{
    std::vector<vertex> generate_vertex_data();
    VkGraphicsPipelineCreateInfo get_pipeline_create_info(VkDevice logical_device, const std::vector<VkShaderModule>& shaders, VkPipelineLayout pipeline_layout, VkRenderPass render_pass, const VkExtent2D& window_extent);
    void generate_model_data(std::span<cone_instance>& cones, glm::vec3 min_range, glm::vec3 max_range);
}