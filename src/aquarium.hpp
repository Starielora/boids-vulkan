#pragma once

#include "boids.hpp"
#include "shader_module_cache.hpp"

#include <volk.h>

#include <tuple>
#include <array>

namespace aquarium
{
    std::tuple<bool, const glm::vec3&> check_collision(const glm::vec4& pos, const glm::vec3& min_range, const glm::vec3& max_range);
    std::array<boids::plane_repellent, 6> get_wall_repellents(const glm::vec3& min_range, const glm::vec3& max_range, float& force_weight);
    VkGraphicsPipelineCreateInfo get_pipeline_create_info(VkDevice logical_device, VkPipelineLayout pipeline_layout, VkRenderPass render_pass, const VkExtent2D& window_extent, shaders::module_cache& shaders_cache);
}
