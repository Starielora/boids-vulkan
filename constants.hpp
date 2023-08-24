#pragma once

#include <Volk/volk.h>

constexpr auto msaa_samples = VK_SAMPLE_COUNT_8_BIT; // TODO query device
constexpr auto depth_format = VK_FORMAT_D32_SFLOAT; // TODO query device support
