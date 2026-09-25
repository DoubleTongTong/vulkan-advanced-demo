#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <vector>

class Camera;
class VulkanBuffer;
class VulkanContext;
class VulkanRenderPipeline;
class VulkanSwapchain;

// 每帧收集调试线段，并在已经开启的场景 dynamic rendering 中绘制。
// 线段数据只在 CPU 侧保存当前帧，clear() 后可立即开始构建下一批图元。
class LineCanvas3D {
public:
    LineCanvas3D(
        const VulkanContext& context,
        const VulkanSwapchain& swapchain,
        const Camera& camera);

    LineCanvas3D(const LineCanvas3D&) = delete;
    LineCanvas3D& operator=(const LineCanvas3D&) = delete;

    void clear();
    void line(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color);
    void plane(
        const glm::vec3& origin,
        const glm::vec3& axis1,
        const glm::vec3& axis2,
        int lines1,
        int lines2,
        float size1,
        float size2,
        const glm::vec4& color,
        const glm::vec4& outlineColor);
    void box(
        const glm::mat4& model,
        const glm::vec3& halfExtent,
        const glm::vec4& color);
    void frustum(
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec4& color);

    // 调用方必须已开启与 swapchain/depth attachment 对应的 dynamic rendering。
    void record(VkCommandBuffer commandBuffer, uint32_t imageIndex);

private:
    struct Vertex {
        glm::vec4 position{0.0f};
        glm::vec4 color{1.0f};
    };

    void ensureBuffer(uint32_t imageIndex, VkDeviceSize requiredSize);
    glm::mat4 viewProjection() const;

    const VulkanContext& context_;
    const VulkanSwapchain& swapchain_;
    const Camera& camera_;
    std::vector<Vertex> vertices_;
    std::vector<std::unique_ptr<VulkanBuffer>> buffers_;
    std::vector<VkDeviceSize> bufferCapacities_;
    std::unique_ptr<VulkanRenderPipeline> pipeline_;
};
