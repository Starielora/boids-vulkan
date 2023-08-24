#pragma once

#include "cleanup.hpp"

#include <Volk/volk.h>
#include <GLFW/glfw3.h>

#include <optional>
#include <string_view>
#include <vector>
#include <tuple>

namespace window
{
    GLFWwindow* create(cleanup::queue_type& cleanup_queue, const GLFWcursorposfun& mouse_callback, GLFWkeyfun key_callback);
    std::vector<const char*> get_vk_extensions();
    VkSurfaceKHR create_vk_surface(VkInstance vk_instance, GLFWwindow* window, cleanup::queue_type& cleanup_queue);
    VkExtent2D get_extent(GLFWwindow* window);
}

VkBool32 debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);
void create_debug_utils_messenger(VkInstance vk_instance, cleanup::queue_type& cleanup_queue);
VkInstance create_vulkan_instance(const std::vector<const char*>& layers, const std::vector<const char*>& extensions, cleanup::queue_type& cleanup_queue);
bool check_instance_layers(const std::vector<const char*>& requested_layers);
std::optional<uint32_t> pick_family_index(VkQueueFlagBits bits, const std::vector<VkQueueFamilyProperties>& queue_props);
bool check_device_extensions(VkPhysicalDevice device, const std::vector<const char*> required_device_extensions);
std::tuple<VkPhysicalDevice, uint32_t, VkPhysicalDeviceProperties> pick_physical_device(VkInstance instance, VkSurfaceKHR surface, const std::vector<const char*> required_device_extensions);
std::tuple<VkDevice, VkQueue> create_logical_device(VkPhysicalDevice physical_device, uint32_t queue_family_index, const std::vector<const char*>& device_extensions, cleanup::queue_type& cleanup_queue);
VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR& surface_caps, VkExtent2D glfw_framebuffer_extent);
VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>& available_present_modes);
VkSurfaceFormatKHR choose_image_format(const std::vector<VkSurfaceFormatKHR>& available_formats);
std::tuple<VkSwapchainKHR, VkSurfaceFormatKHR> create_swapchain(VkDevice logical_device, VkPhysicalDevice physical_device, VkSurfaceKHR surface, uint32_t queue_family_index, VkExtent2D glfw_framebuffer_extent, cleanup::queue_type& cleanup_queue);
VkImageView create_color_image_view(VkDevice logical_device, VkFormat format, VkImage image, cleanup::queue_type& cleanup_queue);
std::tuple<std::vector<VkImage>, std::vector<VkImageView>> get_swapchain_images(VkDevice logical_device, VkSwapchainKHR swapchain, VkFormat image_format, cleanup::queue_type& cleanup_queue);
std::vector<uint8_t> read_file(const std::string_view filename);
std::vector<VkShaderModule> load_shaders(VkDevice logical_device, std::initializer_list<std::string_view> shader_paths, cleanup::queue_type& cleanup_queue);
VkShaderModule create_shader_module(VkDevice logical_device, const std::vector<uint8_t>& code, cleanup::queue_type& cleanup_queue);
VkRenderPass create_render_pass(VkDevice logical_device, VkFormat swapchain_format, VkFormat depth_format, VkSampleCountFlagBits samples, cleanup::queue_type& cleanup_queue);
VkPipelineLayout create_pipeline_layout(VkDevice logical_device, const std::vector<VkDescriptorSetLayout>& set_layouts, cleanup::queue_type& cleanup_queue);
std::vector<VkPipeline> create_graphics_pipelines(VkDevice logical_device, const std::vector<VkGraphicsPipelineCreateInfo>& create_infos, cleanup::queue_type& cleanup_queue);
std::vector<VkFramebuffer> create_swapchain_framebuffers(VkDevice logical_device, VkRenderPass render_pass, const std::vector<VkImageView>& color_imageviews, const std::vector<VkImageView>& swapchain_imageviews, const std::vector<VkImageView> depth_image_views, VkExtent2D swapchain_extent, cleanup::queue_type& cleanup_queue);
VkCommandPool create_command_pool(VkDevice logical_device, uint32_t queue_family_index, cleanup::queue_type& cleanup_queue);
std::vector<VkCommandBuffer> create_command_buffers(VkDevice logical_device, VkCommandPool command_pool, uint32_t count, cleanup::queue_type& cleanup_queue);
std::vector<VkSemaphore> create_semaphores(VkDevice logical_device, uint32_t count, cleanup::queue_type& cleanup_queue);
std::vector<VkFence> create_fences(VkDevice logical_device, uint32_t count, cleanup::queue_type& cleanup_queue);
uint32_t find_memory_type_index(VkPhysicalDevice physical_device, uint32_t memory_type_requirements, VkMemoryPropertyFlags memory_property_flags);
VkDeviceMemory allocate_memory(VkDevice logical_device, std::size_t size, uint32_t memory_type_index, cleanup::queue_type& cleanup_queue);
std::tuple<VkImage, VkMemoryRequirements> create_color_image(VkDevice logical_device, VkFormat swapchain_format, VkExtent2D swapchain_extent, cleanup::queue_type& cleanup_queue);
std::tuple<VkImage, VkImageView, VkDeviceMemory> create_color_image(VkDevice logical_device, VkPhysicalDevice physical_device, VkFormat swapchain_format, VkExtent2D swapchain_extent, cleanup::queue_type& cleanup_queue);
std::tuple<VkImage, VkMemoryRequirements> create_depth_image(VkDevice logical_device, VkExtent2D swapchain_extent, cleanup::queue_type& cleanup_queue);
VkImageView create_depth_image_view(VkDevice logical_device, VkImage image, cleanup::queue_type& cleanup_queue);
std::tuple<VkImage, VkImageView, VkDeviceMemory> create_depth_image(VkDevice logical_device, VkPhysicalDevice physical_device, VkExtent2D swapchain_extent, cleanup::queue_type& cleanup_queue);
std::tuple<VkBuffer, VkMemoryRequirements> create_buffer(VkDevice logical_device, std::size_t size, VkBufferUsageFlags usage, cleanup::queue_type& cleanup_queue);
std::tuple<VkBuffer, VkDeviceMemory> create_buffer(VkDevice logical_device, VkPhysicalDevice physical_device, std::size_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_flags, cleanup::queue_type& cleanup_queue);
void copy_memory(VkDevice logical_device, VkDeviceMemory device_memory, uint32_t offset, const void* in_data, std::size_t size);
VkDescriptorSetLayout create_descriptor_sets_layouts(VkDevice logical_device, cleanup::queue_type& cleanup_queue);
VkDescriptorPool create_descriptor_pool(VkDevice logical_device, cleanup::queue_type& cleanup_queue);
std::vector<VkDescriptorSet> allocate_descriptor_sets(VkDevice logical_device, const std::vector<VkDescriptorSetLayout>& in_set_layouts, const VkDescriptorPool& pool, std::size_t frame_overlap);
VkDescriptorUpdateTemplate create_descriptor_update_template(VkDevice logical_device, VkDescriptorSetLayout set_layout, VkPipelineLayout pipeline_layout, cleanup::queue_type& cleanup_queue);
std::vector<VkDescriptorBufferInfo> get_descriptor_buffer_infos(VkBuffer buffer, std::size_t size, std::size_t frame_overlap);
std::size_t pad_uniform_buffer_size(std::size_t original_size, std::size_t min_uniform_buffer_alignment);
