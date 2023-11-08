#include "boids_update.hpp"
#include "shaders/shaders.h"
#include "vkcheck.hpp"

namespace boids_update
{
    VkPipelineLayout get_pipeline_layout(VkDevice logical_device, VkDescriptorSetLayout layout, cleanup::queue_type& cleanup_queue)
    {
        const auto pipeline_layout_create_info = VkPipelineLayoutCreateInfo{
             .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
             .pNext = nullptr,
             .flags = {},
             .setLayoutCount = 1,
             .pSetLayouts = &layout,
             .pushConstantRangeCount = 0,
             .pPushConstantRanges = nullptr
        };

        auto pipeline_layout = VkPipelineLayout{};
        VK_CHECK(vkCreatePipelineLayout(logical_device, &pipeline_layout_create_info, nullptr, &pipeline_layout));

        cleanup_queue.push([logical_device, pipeline_layout] {
            vkDestroyPipelineLayout(logical_device, pipeline_layout, nullptr);
        });

        return pipeline_layout;
    }

    VkComputePipelineCreateInfo get_pipeline_create_info(VkDevice logical_device, VkPipelineLayout pipeline_layout, shaders::module_cache& shaders_cache)
    {
        static const auto create_info = VkComputePipelineCreateInfo{
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .pNext = nullptr,
            .flags = {},
            .stage = VkPipelineShaderStageCreateInfo{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = {},
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = shaders_cache.get_module(shader_path::compute::update),
                .pName = "main",
                .pSpecializationInfo = nullptr,
            },
            .layout = pipeline_layout,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = 0
        };

        return create_info;
    }
}