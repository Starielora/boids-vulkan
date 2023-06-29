cmake_minimum_required(VERSION 3.26)

find_package(Vulkan REQUIRED GLOBAL)

add_subdirectory(glfw)
add_subdirectory(glm)

if (WIN32)
   set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_WIN32_KHR)
endif()
add_subdirectory(volk)

add_subdirectory(spdlog)
