#include "gui.hpp"
#include "constants.hpp"
#include "boids.hpp"
#include "vkcheck.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <spdlog/spdlog.h>
#include <spdlog/fmt/compile.h>

#include <span>

namespace gui
{
    VkDescriptorPool create_descriptor_pool(VkDevice logical_device, cleanup::queue_type& cleanup_queue)
    {
        // from imgui vulkan_glfw example
        // this is far too much, I reckon
        const auto pool_sizes = std::array {
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
        };

        const auto create_info = VkDescriptorPoolCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
            .maxSets = 1000,
            .poolSizeCount = pool_sizes.size(),
            .pPoolSizes = pool_sizes.data()
        };

        auto pool = VkDescriptorPool{};
        VK_CHECK(vkCreateDescriptorPool(logical_device, &create_info, nullptr, &pool));

        cleanup_queue.push([logical_device, pool]() { vkDestroyDescriptorPool(logical_device, pool, nullptr); });

        return pool;
    }

    void init(GLFWwindow* window, VkInstance vk_instance, VkDevice logical_device, VkPhysicalDevice physical_device, uint32_t queue_family_index, VkQueue queue, uint32_t images_count, VkRenderPass render_pass, VkSurfaceKHR surface, VkSurfaceFormatKHR surface_format, VkSwapchainKHR swapchain, VkCommandPool command_pool, VkCommandBuffer command_buffer, cleanup::queue_type& cleanup_queue)
    {
        const auto descriptor_pool = gui::create_descriptor_pool(logical_device, cleanup_queue);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;

        ImGui::StyleColorsDark();
        ImGui_ImplGlfw_InitForVulkan(window, true);
        // TODO this is because of volk - https://github.com/ocornut/imgui/issues/4854 https://github.com/ocornut/imgui/pull/6582
        ImGui_ImplVulkan_LoadFunctions([](const char* function_name, void* vulkan_instance) { return vkGetInstanceProcAddr(*(reinterpret_cast<VkInstance*>(vulkan_instance)), function_name); }, &vk_instance);
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = vk_instance;
        init_info.PhysicalDevice = physical_device;
        init_info.Device = logical_device;
        init_info.QueueFamily = queue_family_index;
        init_info.Queue = queue;
        init_info.PipelineCache = VK_NULL_HANDLE;
        init_info.DescriptorPool = descriptor_pool;
        init_info.Subpass = 0;
        init_info.MinImageCount = images_count,
        init_info.ImageCount = images_count,
        init_info.MSAASamples = msaa_samples;
        init_info.Allocator = nullptr;
        init_info.CheckVkResultFn = [](VkResult r) { VK_CHECK(r); };
        ImGui_ImplVulkan_Init(&init_info, render_pass);

        auto imgui_vulkan_window = ImGui_ImplVulkanH_Window();
        imgui_vulkan_window.Surface = surface;
        imgui_vulkan_window.SurfaceFormat = surface_format;
        imgui_vulkan_window.PresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
        imgui_vulkan_window.Swapchain = swapchain;

        {
            VK_CHECK(vkResetCommandPool(logical_device, command_pool, 0));
            VkCommandBufferBeginInfo begin_info = {};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));

            ImGui_ImplVulkan_CreateFontsTexture(command_buffer);

            VkSubmitInfo end_info = {};
            end_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            end_info.commandBufferCount = 1;
            end_info.pCommandBuffers = &command_buffer;
            VK_CHECK(vkEndCommandBuffer(command_buffer));
            VK_CHECK(vkQueueSubmit(queue, 1, &end_info, VK_NULL_HANDLE));
            VK_CHECK(vkDeviceWaitIdle(logical_device));
            ImGui_ImplVulkan_DestroyFontUploadObjects();
        }

        cleanup_queue.push([]() {
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
        });
    }

    void draw(VkCommandBuffer command_buffer, data_refs& data)
    {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        auto& [
            model_scale,
            model_speed,
            camera,
            cohesion_weight,
            separation_weight,
            alignment_weight,
            visual_range,
            wall_force_weight,
            cones,
            dir_lights,
            point_lights,
            fps
        ] = data;

        ImGui::Text("FPS");
        ImGui::PlotLines("fps", fps.data(), fps.size());
        ImGui::Text("Camera");
        static constexpr auto vec3_format = FMT_COMPILE("({: .2f}, {: .2f}, {: .2f})");
        static constexpr auto vec4_format = FMT_COMPILE("({: .2f}, {: .2f}, {: .2f}, {: .2f})");
        static constexpr auto aligned_vectors_format = FMT_COMPILE("{: <10} {:>}");
        const auto pos_str = fmt::format(vec3_format, camera.position().x, camera.position().y, camera.position().z);
        const auto up_str = fmt::format(vec3_format, camera.up().x, camera.up().y, camera.up().z);
        const auto front_str = fmt::format(vec3_format, camera.front().x, camera.front().y, camera.front().z);
        const auto right_str = fmt::format(vec3_format, camera.right().x, camera.right().y, camera.right().z);
        ImGui::Text(fmt::format(aligned_vectors_format, "pos:", pos_str).c_str());
        ImGui::Text(fmt::format(aligned_vectors_format, "up:", up_str).c_str());
        ImGui::Text(fmt::format(aligned_vectors_format, "front:", front_str).c_str());
        ImGui::Text(fmt::format(aligned_vectors_format, "right:", right_str).c_str());

        ImGui::Text("Boids params");
        ImGui::Separator();
        ImGui::DragFloat("Scale", &model_scale, 0.01, 0.01f, 1.f);
        ImGui::Separator();
        ImGui::DragFloat("Speed", &model_speed, 0.001, -1.f, 1.f);
        ImGui::Separator();
        ImGui::DragFloat("Cohesion", &cohesion_weight, 0.001, 0.f, 1.f);
        ImGui::Separator();
        ImGui::DragFloat("Separation", &separation_weight, 0.001f, 0.f, 1.f);
        ImGui::Separator();
        ImGui::DragFloat("Alignment", &alignment_weight, 0.001f, 0.f, 1.f);
        ImGui::Separator();
        ImGui::DragFloat("Visual range", &visual_range, 0.1f, 0.f, 30.f);
        ImGui::Separator();
        ImGui::DragFloat("Wall force", &wall_force_weight, 0.01f, 0.f, 1.f);

        if (ImGui::CollapsingHeader(fmt::format("lights [{}]", dir_lights.size() + point_lights.size()).c_str()))
        {
            for (std::size_t i = 0; i < dir_lights.size(); ++i)
            {
                if (ImGui::TreeNode(fmt::format("Dir light {}", i).c_str()))
                {
                    auto& light = dir_lights[i];

                    ImGui::DragFloat3("Direction", &light.direction[0], 0.01, -1.f, 1.f);
                    ImGui::ColorEdit4("Ambient", &light.ambient[0], ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
                    ImGui::ColorEdit4("Diffuse", &light.diffuse[0], ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
                    ImGui::ColorEdit4("Specular", &light.specular[0], ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
                    ImGui::TreePop();
                }
            }

            for (std::size_t i = 0; i < point_lights.size(); ++i)
            {
                if (ImGui::TreeNode(fmt::format("Point light {}", i).c_str()))
                {
                    auto& light = point_lights[i];

                    ImGui::DragFloat3("Position", &light.position[0], 0.01, -30.f, 30.f);
                    ImGui::ColorEdit4("Ambient", &light.ambient[0], ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
                    ImGui::ColorEdit4("Diffuse", &light.diffuse[0], ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
                    ImGui::ColorEdit4("Specular", &light.specular[0], ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
                    ImGui::DragFloat("Linear", &light.linear, 0.01, 0.0f, 1.0);
                    ImGui::DragFloat("Quadratic", &light.quadratic, 0.01, 0.0f, 1.0);
                    ImGui::TreePop();
                }
            }
        }

        if (ImGui::CollapsingHeader(fmt::format("Instances [{}]", cones.size()).c_str()))
        {
            for (std::size_t i = 0; i < cones.size(); ++i)
            {
                if (ImGui::TreeNode(fmt::format("Instance {}", i).c_str()))
                {
                    auto& cone = cones[i];
                    const auto pos_str = fmt::format(vec3_format, cone.position.x, cone.position.y, cone.position.z);
                    const auto dir_str = fmt::format(vec3_format, cone.direction.x, cone.direction.y, cone.direction.z);
                    const auto color_str = fmt::format(vec4_format, cone.color.x, cone.color.y, cone.color.z, cone.color.w);
                    const auto velocity_str = fmt::format(vec3_format, cone.velocity.x, cone.velocity.y, cone.velocity.z);
                    ImGui::Text(fmt::format(aligned_vectors_format, "pos:", pos_str).c_str());
                    ImGui::Text(fmt::format(aligned_vectors_format, "dir:", dir_str).c_str());
                    ImGui::Text(fmt::format(aligned_vectors_format, "velocity:", velocity_str).c_str());
                    ImGui::Text(fmt::format(aligned_vectors_format, "color:", color_str).c_str());
                    ImGui::SameLine();
                    ImGui::ColorEdit4("", &cone.color[0], ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
                    ImGui::TreePop();
                }
            }
        }

        //ImGui::ShowDemoWindow();

        ImGui::Render();
        ImDrawData* draw_data = ImGui::GetDrawData();
        ImGui_ImplVulkan_RenderDrawData(draw_data, command_buffer);
    }
}
