#pragma once

#include <spdlog/spdlog.h>

// TODO error message
#define VK_CHECK(f) do { const auto result = f; if(result != VK_SUCCESS) {spdlog::error("{}: {}", #f, result); throw std::runtime_error("");}} while(0)
