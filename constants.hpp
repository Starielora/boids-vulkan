#pragma once

#define VK_NO_PROTOTYPES
#include <Volk/volk.h>
#include <glm/glm.hpp>

constexpr auto msaa_samples = VK_SAMPLE_COUNT_8_BIT; // TODO query device
constexpr auto depth_format = VK_FORMAT_D32_SFLOAT; // TODO query device support

struct vertex
{
    glm::vec3 pos;
    glm::vec3 normal;
};
