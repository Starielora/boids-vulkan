#pragma once

#include <Volk/volk.h>

#include <unordered_map>
#include <string>

namespace shaders
{
    class module_cache final
    {
    public:
        explicit module_cache(VkDevice);
        ~module_cache();

        module_cache(const module_cache&) = delete;
        module_cache(module_cache&&) = delete;
        module_cache& operator=(const module_cache&) = delete;
        module_cache& operator=(module_cache&&) = delete;

        VkShaderModule get_module(std::string_view spirv_file);
        void clear();

    private:
        VkDevice _device;
        std::unordered_map<std::string_view, VkShaderModule> _cache;
    };
}
