cmake_minimum_required(VERSION 3.26)

find_package(Vulkan REQUIRED GLOBAL)

add_subdirectory(glfw)
add_subdirectory(glm)

if (WIN32)
   set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_WIN32_KHR)
endif()
add_subdirectory(volk)

add_subdirectory(spdlog)

add_library(imgui STATIC
    imgui/imconfig.h
    imgui/imgui_demo.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_internal.h
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui.cpp
    imgui/imgui.h
    imgui/imstb_rectpack.h
    imgui/imstb_textedit.h
    imgui/imstb_truetype.h

    # backends
    imgui/backends/imgui_impl_glfw.cpp
    imgui/backends/imgui_impl_glfw.h
    imgui/backends/imgui_impl_vulkan.h
    imgui/backends/imgui_impl_vulkan.cpp
)

target_include_directories(imgui PUBLIC imgui imgui/backends)
target_link_libraries(imgui PRIVATE glfw Vulkan::Vulkan)
# im using volk - https://github.com/ocornut/imgui/issues/4854 https://github.com/ocornut/imgui/pull/6582
target_compile_definitions(imgui PRIVATE VK_NO_PROTOTYPES)

# SPIRV-Reflect uses CMAKE_CURRENT_SOURCE_DIR for paths which prevents adding from arbitrary directory using add_subdirectory
add_library(spirv-reflect-static STATIC
    SPIRV-Reflect/spirv_reflect.h
    SPIRV-Reflect/spirv_reflect.cpp
)

target_include_directories(spirv-reflect-static PUBLIC SPIRV-Reflect)