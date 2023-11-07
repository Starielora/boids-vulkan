#include "shader_module_cache.hpp"
#include "cleanup.hpp"

#include <Volk/volk.h>

namespace boids_update
{
    VkPipelineLayout get_pipeline_layout(VkDevice logical_device, cleanup::queue_type& cleanup_queue);
    VkComputePipelineCreateInfo get_pipeline_create_info(VkDevice logical_device, VkPipelineLayout pipeline_layout, shaders::module_cache& shaders_cache);
}
