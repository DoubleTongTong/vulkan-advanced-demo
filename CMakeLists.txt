cmake_minimum_required(VERSION 3.24)

project(vulkan_advanced_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# 把 FetchContent 拉下来的第三方代码统一放在项目根目录的 externals/。
# 这样目录结构更直观，也方便你以后快速确认依赖来自哪里。
set(FETCHCONTENT_BASE_DIR ${CMAKE_SOURCE_DIR}/externals)

# GLFW 只负责窗口、输入和 Vulkan surface 相关能力。
# Vulkan 实例和渲染管线后面我们自己初始化，所以这里不用 OpenGL。
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)
FetchContent_MakeAvailable(glfw)

add_executable(vulkan_advanced_demo
    src/main.cpp
    src/GlfwWindow.cpp
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
)
