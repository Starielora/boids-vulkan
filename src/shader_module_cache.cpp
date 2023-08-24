#include "shader_module_cache.hpp"
#include "vkcheck.hpp"

#include <spdlog/spdlog.h>

#include <cassert>
#include <fstream>

namespace shaders
{
    namespace
    {
        std::vector<uint8_t> read_file(const std::string_view filename)
        {
            spdlog::debug("Reading file: {}", filename);
            auto file = std::ifstream(filename.data(), std::ios::binary);
            if (!file.is_open())
            {
                spdlog::error("Could not open file {}", filename);
                throw std::runtime_error("");
            }
            return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }
    }

    module_cache::module_cache(VkDevice device) : _device(device)
    {
        assert(_device != VK_NULL_HANDLE);
    }

    module_cache::~module_cache()
    {
        clear();
    }

    VkShaderModule module_cache::get_module(std::string_view spirv_file)
    {
        if (const auto cached_shader_module = _cache.find(spirv_file); cached_shader_module != _cache.end())
        {
            return cached_shader_module->second;
        }

        const auto shader_code = read_file(spirv_file);
        assert(!shader_code.empty());

        const auto create_info = VkShaderModuleCreateInfo{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .codeSize = shader_code.size(),
            .pCode = reinterpret_cast<const uint32_t*>(shader_code.data())
        };

        auto shader_module = VkShaderModule{};
        VK_CHECK(vkCreateShaderModule(_device, &create_info, nullptr, &shader_module));

        _cache.emplace(spirv_file, shader_module);

        return shader_module;
    }

    void module_cache::clear()
    {
        for (const auto& [_, shader_module] : _cache)
        {
            vkDestroyShaderModule(_device, shader_module, nullptr);
        }

        _cache.clear();
    }
}
