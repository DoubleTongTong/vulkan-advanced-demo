#include "rendering/RubberDuckCommandRecorder.h"

#include "mesh/GeometryCache.h"
#include "VulkanShaderModule.h"
#include "VulkanSwapchain.h"
#include "VulkanContext.h"
#include "VulkanUtils.h"

#include <glm/ext/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

std::vector<uint8_t> specializationData(uint32_t value) {
    std::vector<uint8_t> data(sizeof(value));
    std::memcpy(data.data(), &value, sizeof(value));
    return data;
}

constexpr std::array<float, 1> SimplificationRatios = {0.2f};

glm::mat4 makeModelViewProjection(
    const RenderView& view,
    const float center[3],
    float radius,
    const glm::vec3& offset,
    float scaleMultiplier) {
    using Clock = std::chrono::steady_clock;
    static const Clock::time_point startTime = Clock::now();

    const float seconds = std::chrono::duration<float>(Clock::now() - startTime).count();
    // glTF 本身是 Y-up 坐标系，Assimp 已经把节点变换预处理到顶点里了。
    // 这里不再额外绕 X 轴转 90 度，只做居中、缩放和一个水平观赏角。
    const float scale = 1.0f / radius;
    const float rotationAngle = glm::radians(35.0f) + seconds;
    const glm::mat4 model =
        glm::translate(glm::mat4(1.0f), offset) *
        glm::rotate(glm::mat4(1.0f), rotationAngle, glm::vec3(0.0f, 1.0f, 0.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(scale * scaleMultiplier)) *
        glm::translate(glm::mat4(1.0f), glm::vec3(-center[0], -center[1], -center[2]));
    return view.viewProjection * model;
}

} // namespace

RubberDuckCommandRecorder::RubberDuckCommandRecorder(
    const VulkanContext& context,
    const VulkanSwapchain& swapchain,
    const VulkanShaderModule& vertexShader,
    const VulkanShaderModule& fragmentShader,
    const std::filesystem::path& scenePath)
    : RubberDuckCommandRecorder(
          context,
          swapchain,
          vertexShader,
          fragmentShader,
          GeometryCache::loadOrConvert(scenePath, SimplificationRatios)) {
}

RubberDuckCommandRecorder::RubberDuckCommandRecorder(
    const VulkanContext& context,
    const VulkanSwapchain& swapchain,
    const VulkanShaderModule& vertexShader,
    const VulkanShaderModule& fragmentShader,
    MeshData&& mesh)
    : context_(context),
      swapchain_(swapchain),
      vertexBuffer_(
          context,
          {
              .usage = BufferUsage_Vertex,
              .storage = BufferStorage::Device,
              .size = mesh.vertexBytes().size(),
              .data = mesh.vertexBytes().data(),
              .debugName = "Rubber duck vertex buffer",
          }),
      solidPipeline_(
          context,
          {
              .vertexShader = &vertexShader,
              .fragmentShader = &fragmentShader,
              .vertexBindings = {
                  {
                      .binding = 0,
                      .stride = sizeof(MeshVertex),
                      .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
                  },
              },
              .vertexAttributes = {
                  {
                      .location = 0,
                      .binding = 0,
                      .format = VK_FORMAT_R32G32B32_SFLOAT,
                      .offset = 0,
                  },
              },
              .colorFormat = swapchain.imageFormat(),
              .depthFormat = VulkanDepthAttachment::DefaultFormat,
              .cullMode = VK_CULL_MODE_BACK_BIT,
              .depthTestEnabled = true,
              .depthWriteEnabled = true,
              .specializationEntries = {
                  {
                      .constantID = 0,
                      .offset = 0,
                      .size = sizeof(uint32_t),
                  },
              },
              .specializationData = specializationData(0),
              .debugName = "Rubber duck solid pipeline",
          }),
      wireframePipeline_(
          context,
          {
              .vertexShader = &vertexShader,
              .fragmentShader = &fragmentShader,
              .vertexBindings = {
                  {
                      .binding = 0,
                      .stride = sizeof(MeshVertex),
                      .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
                  },
              },
              .vertexAttributes = {
                  {
                      .location = 0,
                      .binding = 0,
                      .format = VK_FORMAT_R32G32B32_SFLOAT,
                      .offset = 0,
                  },
              },
              .colorFormat = swapchain.imageFormat(),
              .depthFormat = VulkanDepthAttachment::DefaultFormat,
              .cullMode = VK_CULL_MODE_BACK_BIT,
              .polygonMode = VK_POLYGON_MODE_LINE,
              .depthTestEnabled = true,
              .depthWriteEnabled = false,
              .depthBiasEnabled = true,
              .depthBiasConstantFactor = -1.0f,
              .depthBiasSlopeFactor = -1.0f,
              .specializationEntries = {
                  {
                      .constantID = 0,
                      .offset = 0,
                      .size = sizeof(uint32_t),
                  },
              },
              .specializationData = specializationData(1),
              .debugName = "Rubber duck wireframe pipeline",
          }),
      depthAttachment_(context, swapchain.extent(), "Rubber duck depth"),
      meshCenter_{mesh.bounds().center[0], mesh.bounds().center[1], mesh.bounds().center[2]},
      meshRadius_(mesh.bounds().radius),
      imageLayouts_(swapchain.images().size(), VK_IMAGE_LAYOUT_UNDEFINED) {
    if ((swapchain.imageUsage() & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
        throw std::runtime_error("Swapchain images do not support color attachment usage.");
    }

    createIndexBuffer(mesh);
}

void RubberDuckCommandRecorder::createIndexBuffer(const MeshData& mesh) {
    // 所有 LOD 共用一次上传；绘制时只改变绑定偏移和索引数。
    indexBuffer_ = std::make_unique<VulkanBuffer>(
        context_,
        BufferDesc{
            .usage = BufferUsage_Index,
            .storage = BufferStorage::Device,
            .size = mesh.indexBytes().size(),
            .data = mesh.indexBytes().data(),
            .debugName = "Rubber duck packed LOD indices",
        });
    const MeshDescriptor& descriptor = mesh.descriptor();
    indexOffsets_.reserve(descriptor.lodCount);
    indexCounts_.reserve(descriptor.lodCount);
    for (uint32_t lod = 0; lod < descriptor.lodCount; ++lod) {
        indexOffsets_.push_back(sizeof(uint32_t) * descriptor.lodOffsets[lod]);
        indexCounts_.push_back(descriptor.lodIndexCount(lod));
    }
}

void RubberDuckCommandRecorder::record(const RenderFrameContext& frame) {
    const VkCommandBuffer commandBuffer = frame.commandBuffer;
    const uint32_t imageIndex = frame.imageIndex;
    const VkImage image = swapchain_.images()[imageIndex];
    const VkImageView imageView = swapchain_.imageViews()[imageIndex];
    const VkExtent2D extent = swapchain_.extent();

    vulkan_utils::transitionImage(
        commandBuffer,
        image,
        VK_IMAGE_ASPECT_COLOR_BIT,
        imageLayouts_[imageIndex],
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        0,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    depthAttachment_.transitionForWrite(commandBuffer);

    const VkClearValue clearColor{
        .color = {{1.0f, 1.0f, 1.0f, 1.0f}},
    };
    const VkRenderingAttachmentInfo colorAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = imageView,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = clearColor,
    };
    const VkRenderingAttachmentInfo depthAttachment = depthAttachment_.renderingInfo();

    const VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {
            .offset = {0, 0},
            .extent = extent,
        },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = &depthAttachment,
    };

    vkCmdBeginRendering(commandBuffer, &renderingInfo);

    const VkViewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(extent.width),
        .height = static_cast<float>(extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    const VkRect2D scissor{
        .offset = {0, 0},
        .extent = extent,
    };
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    const VkBuffer vertexBuffers[] = {vertexBuffer_.handle()};
    const VkDeviceSize vertexOffsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, vertexOffsets);
    constexpr std::array<glm::vec3, 2> LodOffsets = {
        glm::vec3(-0.70f, 0.0f, 0.0f),
        glm::vec3(0.70f, 0.0f, 0.0f),
    };
    const size_t lodCount = std::min(indexCounts_.size(), LodOffsets.size());
    for (size_t lodIndex = 0; lodIndex < lodCount; ++lodIndex) {
        const glm::mat4 mvp = makeModelViewProjection(
            frame.view, meshCenter_, meshRadius_, LodOffsets[lodIndex], 0.55f);
        vkCmdBindIndexBuffer(
            commandBuffer, indexBuffer_->handle(), indexOffsets_[lodIndex], VK_INDEX_TYPE_UINT32);

        solidPipeline_.bind(commandBuffer);
        vkCmdPushConstants(
            commandBuffer,
            solidPipeline_.layout(),
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof(glm::mat4),
            &mvp);
        vkCmdDrawIndexed(commandBuffer, indexCounts_[lodIndex], 1, 0, 0, 0);

        wireframePipeline_.bind(commandBuffer);
        vkCmdPushConstants(
            commandBuffer,
            wireframePipeline_.layout(),
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof(glm::mat4),
            &mvp);
        vkCmdDrawIndexed(commandBuffer, indexCounts_[lodIndex], 1, 0, 0, 0);
    }

    vkCmdEndRendering(commandBuffer);

    vulkan_utils::transitionImage(
        commandBuffer,
        image,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        0,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

    imageLayouts_[imageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
}
