#pragma once

#include "shader_module_cache.hpp"

#include <Volk/volk.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <vector>
#include <array>

struct directional_light
{
    glm::vec4 direction;
    glm::vec4 ambient;
    glm::vec4 diffuse;
    glm::vec4 specular;
};

struct point_light
{
    glm::vec4 position;
    glm::vec4 ambient;
    glm::vec4 diffuse;
    glm::vec4 specular;

    float constant;
    float linear;
    float quadratic;
    float _padding;
};

namespace light
{
    VkGraphicsPipelineCreateInfo get_pipeline_create_info(VkDevice logical_device, VkPipelineLayout pipeline_layout, VkRenderPass render_pass, const VkExtent2D& window_extent, shaders::module_cache& shaders_cache);
}
