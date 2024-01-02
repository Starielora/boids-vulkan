#pragma once

#include "shader_module_cache.hpp"

#include <volk.h>

#include <vector>

namespace grid
{
    VkGraphicsPipelineCreateInfo get_pipeline_create_info(VkDevice logical_device, VkPipelineLayout pipeline_layout, VkRenderPass render_pass, const VkExtent2D& window_extent, shaders::module_cache& shaders_cache);
}
