#include "rendering/canvas/LineCanvas3D.h"

#include "Camera.h"
#include "VulkanBuffer.h"
#include "VulkanContext.h"
#include "VulkanRenderPipeline.h"
#include "VulkanShaderModule.h"
#include "VulkanSwapchain.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <stdexcept>

namespace {

constexpr VkFormat DepthFormat = VK_FORMAT_D32_SFLOAT;

void appendBoxEdges(
    LineCanvas3D& canvas,
    const std::array<glm::vec3, 8>& points,
    const glm::vec4& color) {
    constexpr std::array<std::array<uint32_t, 2>, 12> Edges = {{
        {{0, 1}}, {{2, 3}}, {{4, 5}}, {{6, 7}},
        {{0, 2}}, {{1, 3}}, {{4, 6}}, {{5, 7}},
        {{0, 4}}, {{1, 5}}, {{2, 6}}, {{3, 7}},
    }};
    for (const auto& edge : Edges) {
        canvas.line(points[edge[0]], points[edge[1]], color);
    }
}

} // namespace

LineCanvas3D::LineCanvas3D(
    const VulkanContext& context,
    const VulkanSwapchain& swapchain,
    const Camera& camera)
    : context_(context), swapchain_(swapchain), camera_(camera),
      buffers_(swapchain.images().size()), bufferCapacities_(swapchain.images().size(), 0) {
    const std::filesystem::path shaderDirectory = APP_LINE_CANVAS_SHADER_DIR;
    const VulkanShaderModule vertexShader =
        VulkanShaderModule::fromFile(context_, shaderDirectory / "main.vert");
    const VulkanShaderModule fragmentShader =
        VulkanShaderModule::fromFile(context_, shaderDirectory / "main.frag");

    pipeline_ = std::make_unique<VulkanRenderPipeline>(
        context_,
        RenderPipelineDesc{
            .vertexShader = &vertexShader,
            .fragmentShader = &fragmentShader,
            .vertexBindings = {{
                .binding = 0,
                .stride = sizeof(Vertex),
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
            }},
            .vertexAttributes = {
                {
                    .location = 0,
                    .binding = 0,
                    .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                    .offset = offsetof(Vertex, position),
                },
                {
                    .location = 1,
                    .binding = 0,
                    .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                    .offset = offsetof(Vertex, color),
                },
            },
            .colorFormat = swapchain_.imageFormat(),
            .depthFormat = DepthFormat,
            .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
            .depthTestEnabled = true,
            .depthWriteEnabled = false,
            .blendEnabled = true,
            .debugName = "3D line canvas pipeline",
        });
}

void LineCanvas3D::clear() {
    vertices_.clear();
}

void LineCanvas3D::line(
    const glm::vec3& from,
    const glm::vec3& to,
    const glm::vec4& color) {
    vertices_.push_back({.position = glm::vec4(from, 1.0f), .color = color});
    vertices_.push_back({.position = glm::vec4(to, 1.0f), .color = color});
}

void LineCanvas3D::plane(
    const glm::vec3& origin,
    const glm::vec3& axis1,
    const glm::vec3& axis2,
    int lines1,
    int lines2,
    float size1,
    float size2,
    const glm::vec4& color,
    const glm::vec4& outlineColor) {
    if (lines1 < 1 || lines2 < 1 || size1 <= 0.0f || size2 <= 0.0f) {
        throw std::invalid_argument("Plane line counts and sizes must be positive.");
    }

    const glm::vec3 direction1 = glm::normalize(axis1);
    const glm::vec3 direction2 = glm::normalize(axis2);
    const glm::vec3 half1 = direction1 * (size1 * 0.5f);
    const glm::vec3 half2 = direction2 * (size2 * 0.5f);
    const glm::vec3 minimum = origin - half1 - half2;
    const glm::vec3 maximum = origin + half1 + half2;

    line(minimum, minimum + half2 * 2.0f, outlineColor);
    line(minimum + half1 * 2.0f, maximum, outlineColor);
    line(minimum, minimum + half1 * 2.0f, outlineColor);
    line(minimum + half2 * 2.0f, maximum, outlineColor);

    for (int index = 1; index < lines1; ++index) {
        const float factor = static_cast<float>(index) / static_cast<float>(lines1);
        const glm::vec3 start = minimum + half1 * (2.0f * factor);
        line(start, start + half2 * 2.0f, color);
    }
    for (int index = 1; index < lines2; ++index) {
        const float factor = static_cast<float>(index) / static_cast<float>(lines2);
        const glm::vec3 start = minimum + half2 * (2.0f * factor);
        line(start, start + half1 * 2.0f, color);
    }
}

void LineCanvas3D::box(
    const glm::mat4& model,
    const glm::vec3& halfExtent,
    const glm::vec4& color) {
    std::array<glm::vec3, 8> points = {{
        { halfExtent.x,  halfExtent.y,  halfExtent.z},
        { halfExtent.x,  halfExtent.y, -halfExtent.z},
        { halfExtent.x, -halfExtent.y,  halfExtent.z},
        { halfExtent.x, -halfExtent.y, -halfExtent.z},
        {-halfExtent.x,  halfExtent.y,  halfExtent.z},
        {-halfExtent.x,  halfExtent.y, -halfExtent.z},
        {-halfExtent.x, -halfExtent.y,  halfExtent.z},
        {-halfExtent.x, -halfExtent.y, -halfExtent.z},
    }};
    for (glm::vec3& point : points) {
        point = glm::vec3(model * glm::vec4(point, 1.0f));
    }
    appendBoxEdges(*this, points, color);
}

void LineCanvas3D::frustum(
    const glm::mat4& view,
    const glm::mat4& projection,
    const glm::vec4& color) {
    constexpr std::array<glm::vec3, 8> ClipCorners = {{
        {-1.0f, -1.0f, 0.0f}, { 1.0f, -1.0f, 0.0f},
        { 1.0f,  1.0f, 0.0f}, {-1.0f,  1.0f, 0.0f},
        {-1.0f, -1.0f, 1.0f}, { 1.0f, -1.0f, 1.0f},
        { 1.0f,  1.0f, 1.0f}, {-1.0f,  1.0f, 1.0f},
    }};

    const glm::mat4 inverseViewProjection = glm::inverse(projection * view);
    std::array<glm::vec3, 8> points;
    for (size_t index = 0; index < ClipCorners.size(); ++index) {
        const glm::vec4 point = inverseViewProjection * glm::vec4(ClipCorners[index], 1.0f);
        points[index] = glm::vec3(point) / point.w;
    }
    appendBoxEdges(*this, points, color);
    line(points[0], points[2], color);
    line(points[1], points[3], color);
    line(points[4], points[6], color);
    line(points[5], points[7], color);
}

void LineCanvas3D::record(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    if (vertices_.empty()) {
        return;
    }
    if (imageIndex >= buffers_.size()) {
        throw std::out_of_range("Line canvas swapchain image index is out of range.");
    }

    const VkDeviceSize byteSize = sizeof(Vertex) * vertices_.size();
    ensureBuffer(imageIndex, byteSize);
    buffers_[imageIndex]->bufferSubData(0, static_cast<size_t>(byteSize), vertices_.data());

    const VkExtent2D extent = swapchain_.extent();
    const VkViewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(extent.width),
        .height = static_cast<float>(extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    const VkRect2D scissor{.offset = {0, 0}, .extent = extent};
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    pipeline_->bind(commandBuffer);
    const VkBuffer vertexBuffers[] = {buffers_[imageIndex]->handle()};
    const VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

    const glm::mat4 mvp = viewProjection();
    vkCmdPushConstants(commandBuffer, pipeline_->layout(), VK_SHADER_STAGE_VERTEX_BIT,
        0, sizeof(mvp), &mvp);
    vkCmdDraw(commandBuffer, static_cast<uint32_t>(vertices_.size()), 1, 0, 0);
}

void LineCanvas3D::ensureBuffer(uint32_t imageIndex, VkDeviceSize requiredSize) {
    if (bufferCapacities_[imageIndex] >= requiredSize) {
        return;
    }

    const VkDeviceSize currentCapacity = bufferCapacities_[imageIndex];
    const VkDeviceSize newCapacity = std::max(requiredSize, currentCapacity * 2u);
    buffers_[imageIndex] = std::make_unique<VulkanBuffer>(
        context_,
        BufferDesc{
            .usage = BufferUsage_Vertex,
            .storage = BufferStorage::HostVisible,
            .size = newCapacity,
            .debugName = "3D line canvas vertex buffer",
        });
    bufferCapacities_[imageIndex] = newCapacity;
}

glm::mat4 LineCanvas3D::viewProjection() const {
    const VkExtent2D extent = swapchain_.extent();
    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    // 与场景投影保持相同的 Vulkan [0, 1] 深度范围。
    glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(45.0f), aspect, 0.1f, 1000.0f);
    projection[1][1] *= -1.0f;
    return projection * camera_.viewMatrix();
}
