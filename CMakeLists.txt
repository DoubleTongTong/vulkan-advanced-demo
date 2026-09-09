cmake_minimum_required(VERSION 3.24)

project(vulkan_advanced_demo LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

find_package(Vulkan REQUIRED)

# 把 FetchContent 拉下来的第三方代码统一放在项目根目录的 externals/。
# 这样目录结构更直观，也方便你以后快速确认依赖来自哪里。
set(FETCHCONTENT_BASE_DIR ${CMAKE_SOURCE_DIR}/externals)

# GLFW 只负责窗口、输入和 Vulkan surface 相关能力。
# Vulkan 实例和渲染管线后面我们自己初始化，所以这里不用 OpenGL。
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)

# Taskflow 本身是 header-only，这里不需要它自带的 examples/tests 进入 VS 目标列表。
set(TF_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TF_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)

# glslang 只作为库使用，不需要它自带的命令行工具和测试目标。
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)

# SPIRV-Reflect 用来从 SPIR-V 中提取 push constants、descriptor 等反射信息。
set(SPIRV_REFLECT_EXECUTABLE OFF CACHE BOOL "" FORCE)
set(SPIRV_REFLECT_STATIC_LIB ON CACHE BOOL "" FORCE)
set(SPIRV_REFLECT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_REFLECT_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SPIRV_REFLECT_INSTALL OFF CACHE BOOL "" FORCE)

# KTX-Software 只需要 libktx 写入能力，不构建工具、测试和文档。
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_DOC OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_TESTS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_TOOLS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_LOADTEST_APPS OFF CACHE STRING "" FORCE)
set(KTX_FEATURE_VK_UPLOAD OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)

FetchContent_Declare(
    taskflow
    GIT_REPOSITORY https://github.com/taskflow/taskflow.git
    GIT_TAG v3.8.0
)

FetchContent_Declare(
    glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_TAG vulkan-sdk-1.4.328.1
)

FetchContent_Declare(
    spirv_reflect
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Reflect.git
    GIT_TAG vulkan-sdk-1.4.328.1
)

FetchContent_Declare(
    ktx
    GIT_REPOSITORY https://github.com/KhronosGroup/KTX-Software.git
    GIT_TAG v4.4.0
)

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
)

# BC7 demo 使用 Bistro 数据集中的一张真实纹理做验证。
# assets/ 默认只提交 README；如果没有下载数据集，配置阶段就给出明确提示。
set(BC7_DEMO_INPUT_TEXTURE
    "${CMAKE_SOURCE_DIR}/assets/PropTextures/Bistro/Paris_Painting_01/Paris_Paintings_01_Diff.png"
)

if(NOT EXISTS "${BC7_DEMO_INPUT_TEXTURE}")
    message(FATAL_ERROR
        "BC7 demo requires the Bistro texture dataset, but the sample texture was not found:\n"
        "  ${BC7_DEMO_INPUT_TEXTURE}\n"
        "Please follow assets/README.md to download the Bistro dataset and place its contents under assets/."
    )
endif()

FetchContent_MakeAvailable(glfw)
FetchContent_MakeAvailable(taskflow)
FetchContent_MakeAvailable(glslang)
FetchContent_MakeAvailable(spirv_reflect)
FetchContent_MakeAvailable(ktx)
FetchContent_MakeAvailable(stb)

add_library(shader_compiler
    src/ShaderCompiler.cpp
)

target_include_directories(shader_compiler
    PUBLIC
        include
)

target_compile_options(shader_compiler
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

target_link_libraries(shader_compiler
    PRIVATE
        glslang
        glslang-default-resource-limits
        spirv-reflect-static
)

add_library(image_processor
    src/ImageProcessor.cpp
)

target_include_directories(image_processor
    PUBLIC
        include
    PRIVATE
        ${stb_SOURCE_DIR}
)

target_compile_options(image_processor
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

add_library(texture_compressor
    src/TextureCompressor.cpp
)

target_include_directories(texture_compressor
    PUBLIC
        include
    PRIVATE
        ${ktx_SOURCE_DIR}/lib
)

target_compile_options(texture_compressor
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

target_link_libraries(texture_compressor
    PRIVATE
        image_processor
        ktx
)

add_executable(vulkan_advanced_demo
    src/main.cpp
    src/GlfwWindow.cpp
    src/VulkanContext.cpp
    src/VulkanDebugMessenger.cpp
    src/VulkanFrameSync.cpp
    src/VulkanImmediateCommands.cpp
    src/VulkanSwapchain.cpp
    src/VulkanUtils.cpp
)

target_include_directories(vulkan_advanced_demo
    PRIVATE
        include
)

target_compile_options(vulkan_advanced_demo
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

target_link_libraries(vulkan_advanced_demo
    PRIVATE
        glfw
        Vulkan::Vulkan
)

add_executable(taskflow_demo
    demos/taskflow/TaskflowDemo.cpp
)

target_include_directories(taskflow_demo
    PRIVATE
        ${taskflow_SOURCE_DIR}
)

target_compile_options(taskflow_demo
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

add_executable(glslang_demo
    demos/glslang/GlslangDemo.cpp
)

target_compile_definitions(glslang_demo
    PRIVATE
        DEMO_SHADER_DIR="${CMAKE_SOURCE_DIR}/demos/glslang/shaders"
)

target_compile_options(glslang_demo
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

target_link_libraries(glslang_demo
    PRIVATE
        shader_compiler
)

add_executable(bc7_demo
    demos/bc7/Bc7Demo.cpp
)

target_compile_definitions(bc7_demo
    PRIVATE
        DEMO_BC7_INPUT_TEXTURE="${BC7_DEMO_INPUT_TEXTURE}"
)

target_compile_options(bc7_demo
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

target_include_directories(bc7_demo
    PRIVATE
        include
)

target_link_libraries(bc7_demo
    PRIVATE
        image_processor
        texture_compressor
)
