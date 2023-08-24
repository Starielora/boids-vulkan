#include "cone.hpp"
#include "constants.hpp"
#include "shaders/shaders.h"

#define VK_NO_PROTOTYPES
#include <Volk/volk.h>
#include <glm/glm.hpp>

#include <array>
#include <vector>
#include <span>
#include <numbers>

namespace cone
{
    constexpr auto bindingDescription = VkVertexInputBindingDescription{
        .binding = 0,
        .stride = 2 * sizeof(glm::vec3),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    constexpr auto vertexAttributeDescriptions = std::array{
        VkVertexInputAttributeDescription{
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = 0
        },
        VkVertexInputAttributeDescription{
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = sizeof glm::vec3
        },
    };

    const auto vertex_input_state = VkPipelineVertexInputStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &bindingDescription,
        .vertexAttributeDescriptionCount = vertexAttributeDescriptions.size(),
        .pVertexAttributeDescriptions = vertexAttributeDescriptions.data()
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
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0.f,
        .depthBiasClamp = 0.f,
        .depthBiasSlopeFactor = 0.f,
        .lineWidth = 2.f
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

    std::vector<vertex> generate_vertex_data()
    {
        constexpr auto base_vertices_count = 12;
        constexpr auto angle_step = 2 * std::numbers::pi / base_vertices_count;
        constexpr auto base_triangles_count = base_vertices_count;
        constexpr auto side_triangles_count = base_vertices_count;
        constexpr auto triangles_count = base_triangles_count + side_triangles_count;
        constexpr auto total_vertices_count = triangles_count * 3;

        const auto center_vertex = glm::vec3(0, 0, 0);
        const auto top_vertex = glm::vec3(0, 2.f, 0);

        auto base_vertices = std::vector<glm::vec3>(base_vertices_count);
        // generate circle
        for (std::size_t i = 0; i < base_vertices_count; ++i)
        {
            const auto angle = i * angle_step;
            base_vertices[i] = glm::vec3(std::sin(angle), 0.f, std::cos(angle));
        }

        auto vertices = std::vector<vertex>(total_vertices_count);
        for (std::size_t i = 0; i < base_triangles_count; ++i)
        {
            const auto A = base_vertices[i];
            const auto B = i == base_vertices_count - 1 ? base_vertices[0] : base_vertices[i + 1];
            const auto C = center_vertex;
            const auto AB = B - A;
            const auto AC = C - A;
            const auto normal = glm::normalize(glm::cross(AC, AB));

            vertices[3 * i + 0] = { B, normal };
            vertices[3 * i + 1] = { A, normal };
            vertices[3 * i + 2] = { C, normal };
        }

        for (std::size_t i = 0; i < side_triangles_count; ++i)
        {
            const auto A = base_vertices[i];
            const auto B = i == base_vertices_count - 1 ? base_vertices[0] : base_vertices[i + 1];
            const auto C = top_vertex;
            const auto AB = B - A;
            const auto AC = C - A;
            const auto normal = glm::normalize(glm::cross(AB, AC));

            vertices[3 * base_triangles_count + 3 * i + 0] = { A, normal };
            vertices[3 * base_triangles_count + 3 * i + 1] = { B, normal };
            vertices[3 * base_triangles_count + 3 * i + 2] = { C, normal };
        }

        return vertices;
    }

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

    void generate_model_data(std::span<cone_instance>& cones, glm::vec3 min_range, glm::vec3 max_range)
    {
        auto rd = std::random_device{};
        auto gen = std::mt19937(rd());
        auto dis = std::uniform_real_distribution<>(-1., 1.);
        auto x_dis = std::uniform_real_distribution<>(min_range.x, max_range.x);
        auto y_dis = std::uniform_real_distribution<>(min_range.y, max_range.y);
        auto z_dis = std::uniform_real_distribution<>(min_range.z, max_range.z);

        for (auto& cone : cones)
        {
            cone.position = glm::vec4(x_dis(gen), y_dis(gen), z_dis(gen), 0.);
            cone.direction = glm::normalize(glm::vec4(dis(gen), dis(gen), dis(gen), 0.));
            cone.velocity = cone.direction;
        }
    }
}
