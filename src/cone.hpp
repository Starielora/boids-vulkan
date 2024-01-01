#pragma once

#include "boids.hpp" // huh?
#include "vertex.hpp"
#include "shader_module_cache.hpp"

#include <volk.h>
#include <array>
#include <random>
#include <span>
#include <vector>

namespace cone
{
    std::vector<vertex> generate_vertex_data();
    VkGraphicsPipelineCreateInfo get_pipeline_create_info(VkDevice logical_device, VkPipelineLayout pipeline_layout, VkRenderPass render_pass, const VkExtent2D& window_extent, shaders::module_cache& shaders_cache);
    void generate_model_data(std::span<boids::boid>& cones, glm::vec3 min_range, glm::vec3 max_range);
}
