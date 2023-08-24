#pragma once

#define VK_NO_PROTOTYPES
#include <Volk/volk.h>

#include <vector>

namespace grid
{
    VkGraphicsPipelineCreateInfo get_pipeline_create_info(VkDevice logical_device, const std::vector<VkShaderModule>& shaders, VkPipelineLayout pipeline_layout, VkRenderPass render_pass, const VkExtent2D& window_extent);
}

