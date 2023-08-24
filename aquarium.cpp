#include "aquarium.hpp"
#include "constants.hpp"
#include "shaders/shaders.h"

#include <glm/glm.hpp>

namespace aquarium
{
    //constexpr float scale = 30.f;
    //const auto min_range = glm::vec3(-scale, 0.f, -scale);
    //const auto max_range = glm::vec3(scale, scale, scale);
    //auto force_weight = 0.1;

    struct
    {
        glm::vec3 front = glm::vec3(0, 0, -1);
        glm::vec3 back = glm::vec3(0, 0, 1);
        glm::vec3 top = glm::vec3(0, -1, 0);
        glm::vec3 bottom = glm::vec3(0, 1, 0);
        glm::vec3 left = glm::vec3(1, 0, 0);
        glm::vec3 right = glm::vec3(-1, 0, 0);
    } const inward_faces_normals;

    std::array<boids::plane_repellent, 6> get_wall_repellents(const glm::vec3& min_range, const glm::vec3& max_range, float& force_weight)
    {
        return std::array{
            boids::plane_repellent(inward_faces_normals.front, max_range.z, force_weight),
            boids::plane_repellent(inward_faces_normals.back, min_range.z, force_weight),
            boids::plane_repellent(inward_faces_normals.top, max_range.y, force_weight),
            boids::plane_repellent(inward_faces_normals.bottom, min_range.y, force_weight),
            boids::plane_repellent(inward_faces_normals.right, max_range.x, force_weight),
            boids::plane_repellent(inward_faces_normals.left, min_range.x, force_weight),
        };

    }

    std::tuple<bool, const glm::vec3&> check_collision(const glm::vec4& pos, const glm::vec3& min_range, const glm::vec3& max_range)
    {
        if (pos.x < min_range.x)
            return { true, inward_faces_normals.left };
        else if (pos.x > max_range.x)
            return { true, inward_faces_normals.right };
        else if (pos.y < min_range.y)
            return { true, inward_faces_normals.bottom };
        else if (pos.y > max_range.y)
            return { true, inward_faces_normals.top };
        else if (pos.z < min_range.z)
            return { true, inward_faces_normals.back };
        else if (pos.z > max_range.z)
            return { true, inward_faces_normals.front };

        return { false, {} };
    }

    constexpr auto vertex_input_state = VkPipelineVertexInputStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .vertexBindingDescriptionCount = 0,
        .pVertexBindingDescriptions = nullptr,
        .vertexAttributeDescriptionCount = 0,
        .pVertexAttributeDescriptions = nullptr
    };

    constexpr auto input_assembly_state = VkPipelineInputAssemblyStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE
    };

    auto viewport = VkViewport{
        .x = 0.f,
        .y = 0.f,
        .width = 0.f,
        .height = 0.f,
        .minDepth = 0.f,
        .maxDepth = 1.f
    };

    auto scissors = VkRect2D{
        .offset = VkOffset2D{
            .x = 0,
            .y = 0,
        },
        .extent = {}
    };

    const auto viewport_state = VkPipelineViewportStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissors,
    };

    constexpr auto rasterization_state = VkPipelineRasterizationStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_LINE,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0.f,
        .depthBiasClamp = 0.f,
        .depthBiasSlopeFactor = 0.f,
        .lineWidth = 10.f
    };

    constexpr auto multisample_state = VkPipelineMultisampleStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .rasterizationSamples = msaa_samples,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.f,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE
    };

    constexpr auto depth_stencil_state = VkPipelineDepthStencilStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
        .front = {},
        .back = {},
        .minDepthBounds = 0.f,
        .maxDepthBounds = 1.f
    };

    constexpr auto color_blend_attachment_state = VkPipelineColorBlendAttachmentState{
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };

    const auto color_blend_state = VkPipelineColorBlendStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment_state,
        .blendConstants = {0.f, 0.f, 0.f, 0.f}
    };

    auto shader_stages = std::array{
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = 0,
            .pName = shader_entry_point.data(),
            .pSpecializationInfo = nullptr
        },
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = 0,
            .pName = shader_entry_point.data(),
            .pSpecializationInfo = nullptr
        }
    };

    VkGraphicsPipelineCreateInfo get_pipeline_create_info(VkDevice logical_device, const std::vector<VkShaderModule>& shaders, VkPipelineLayout pipeline_layout, VkRenderPass render_pass, const VkExtent2D& window_extent)
    {
        assert(shaders.size() == shader_stages.size());
        for (std::size_t i = 0; i < shaders.size(); ++i)
            shader_stages[i].module = shaders[i];

        viewport.width = window_extent.width;
        viewport.height = window_extent.height;

        scissors.extent = window_extent;

        const auto create_info = VkGraphicsPipelineCreateInfo{
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stageCount = static_cast<uint32_t>(shader_stages.size()),
            .pStages = shader_stages.data(),
            .pVertexInputState = &vertex_input_state,
            .pInputAssemblyState = &input_assembly_state,
            .pTessellationState = nullptr,
            .pViewportState = &viewport_state,
            .pRasterizationState = &rasterization_state,
            .pMultisampleState = &multisample_state,
            .pDepthStencilState = &depth_stencil_state,
            .pColorBlendState = &color_blend_state,
            .pDynamicState = nullptr,
            .layout = pipeline_layout,
            .renderPass = render_pass,
            .subpass = 0,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = 0
        };

        return create_info;
    }
}
