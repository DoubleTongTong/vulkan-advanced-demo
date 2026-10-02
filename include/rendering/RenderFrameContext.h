#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <vulkan/vulkan.h>

#include <cstdint>

// 一帧内所有 3D 绘制共享的观察结果。渲染器只读取它，不拥有也不更新相机。
struct RenderView {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::mat4 viewProjection{1.0f};
    glm::mat4 inverseViewProjection{1.0f};
    glm::vec3 cameraPosition{0.0f};
};

// 录制命令时需要的逐帧输入；以后也可以自然加入帧时间、场景光照等共享数据。
struct RenderFrameContext {
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    uint32_t imageIndex = 0;
    uint32_t frameIndex = 0;
    const RenderView& view;
};
