#include "rendering/RubberDuckCommandRecorder.h"

#include "VulkanShaderModule.h"
#include "VulkanSwapchain.h"
#include "VulkanContext.h"
#include "VulkanUtils.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <vk_mem_alloc.h>

#include <chrono>
#include <cstring>
#include <stdexcept>

namespace {

std::vector<uint8_t> specializationData(uint32_t value) {
    std::vector<uint8_t> data(sizeof(value));
    std::memcpy(data.data(), &value, sizeof(value));
    return data;
}

constexpr VkFormat DepthFormat = VK_FORMAT_D32_SFLOAT;

glm::mat4 makeModelViewProjection(VkExtent2D extent, const float center[3], float radius) {
    using Clock = std::chrono::steady_clock;
    static const Clock::time_point startTime = Clock::now();

    const float seconds = std::chrono::duration<float>(Clock::now() - startTime).count();
    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);

    glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 1000.0f);
    // GLM 的透视矩阵默认按 OpenGL 裁剪空间生成；Vulkan 的 NDC Y 方向相反，这里翻回来。
    projection[1][1] *= -1.0f;

    // glTF 本身是 Y-up 坐标系，Assimp 已经把节点变换预处理到顶点里了。
    // 这里不再额外绕 X 轴转 90 度，只做居中、缩放和一个水平观赏角。
    const float scale = 1.0f / radius;
    const float rotationAngle = glm::radians(35.0f) + seconds;
    const glm::mat4 model =
        glm::rotate(glm::mat4(1.0f), rotationAngle, glm::vec3(0.0f, 1.0f, 0.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(scale)) *
        glm::translate(glm::mat4(1.0f), glm::vec3(-center[0], -center[1], -center[2]));
    const glm::mat4 view =
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.05f, -3.0f));

    return projection * view * model;
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
          ModelLoader::loadFirstMesh(scenePath)) {
}

RubberDuckCommandRecorder::RubberDuckCommandRecorder(
    const VulkanContext& context,
    const VulkanSwapchain& swapchain,
    const VulkanShaderModule& vertexShader,
    const VulkanShaderModule& fragmentShader,
    ModelMesh&& mesh)
    : context_(context),
      swapchain_(swapchain),
      vertexBuffer_(
          context,
          {
              .usage = BufferUsage_Vertex,
              .storage = BufferStorage::Device,
              .size = sizeof(float) * mesh.positions.size(),
              .data = mesh.positions.data(),
              .debugName = "Rubber duck vertex buffer",
          }),
      indexBuffer_(
          context,
          {
              .usage = BufferUsage_Index,
              .storage = BufferStorage::Device,
              .size = sizeof(uint32_t) * mesh.indices.size(),
              .data = mesh.indices.data(),
              .debugName = "Rubber duck index buffer",
          }),
      solidPipeline_(
          context,
          {
              .vertexShader = &vertexShader,
              .fragmentShader = &fragmentShader,
              .vertexBindings = {
                  {
                      .binding = 0,
                      .stride = sizeof(float) * 3,
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
              .depthFormat = DepthFormat,
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
                      .stride = sizeof(float) * 3,
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
              .depthFormat = DepthFormat,
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
      meshCenter_{mesh.center[0], mesh.center[1], mesh.center[2]},
      meshRadius_(mesh.radius),
      indexCount_(static_cast<uint32_t>(mesh.indices.size())),
      imageLayouts_(swapchain.images().size(), VK_IMAGE_LAYOUT_UNDEFINED) {
    if ((swapchain.imageUsage() & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
        throw std::runtime_error("Swapchain images do not support color attachment usage.");
    }

    createDepthAttachment();
}

RubberDuckCommandRecorder::~RubberDuckCommandRecorder() {
    destroyDepthAttachment();
}

void RubberDuckCommandRecorder::record(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
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
    vulkan_utils::transitionImage(
        commandBuffer,
        depthImage_,
        VK_IMAGE_ASPECT_DEPTH_BIT,
        depthLayout_,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        0,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);

    const VkClearValue clearColor{
        .color = {{1.0f, 1.0f, 1.0f, 1.0f}},
    };
    const VkClearValue clearDepth{
        .depthStencil = {1.0f, 0},
    };

    const VkRenderingAttachmentInfo colorAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = imageView,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = clearColor,
    };
    const VkRenderingAttachmentInfo depthAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = depthImageView_,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue = clearDepth,
    };

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
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer_.handle(), 0, VK_INDEX_TYPE_UINT32);

    const glm::mat4 mvp = makeModelViewProjection(extent, meshCenter_, meshRadius_);
    solidPipeline_.bind(commandBuffer);
    vkCmdPushConstants(
        commandBuffer,
        solidPipeline_.layout(),
        VK_SHADER_STAGE_VERTEX_BIT,
        0,
        sizeof(glm::mat4),
        &mvp);
    vkCmdDrawIndexed(commandBuffer, indexCount_, 1, 0, 0, 0);

    wireframePipeline_.bind(commandBuffer);
    vkCmdPushConstants(
        commandBuffer,
        wireframePipeline_.layout(),
        VK_SHADER_STAGE_VERTEX_BIT,
        0,
        sizeof(glm::mat4),
        &mvp);
    vkCmdDrawIndexed(commandBuffer, indexCount_, 1, 0, 0, 0);

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
    depthLayout_ = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
}

void RubberDuckCommandRecorder::createDepthAttachment() {
    const VkExtent2D extent = swapchain_.extent();
    const VkImageCreateInfo imageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = DepthFormat,
        .extent = {
            .width = extent.width,
            .height = extent.height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    const VmaAllocationCreateInfo allocationCreateInfo{
        .usage = VMA_MEMORY_USAGE_AUTO,
        .preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };

    vulkan_utils::checkVk(
        vmaCreateImage(
            context_.allocator(),
            &imageCreateInfo,
            &allocationCreateInfo,
            &depthImage_,
            &depthAllocation_,
            nullptr),
        "vmaCreateImage");
    context_.setDebugObjectName(
        VK_OBJECT_TYPE_IMAGE,
        reinterpret_cast<uint64_t>(depthImage_),
        "Rubber duck depth image");

    const VkImageViewCreateInfo viewCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = depthImage_,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = DepthFormat,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    vulkan_utils::checkVk(
        vkCreateImageView(context_.device(), &viewCreateInfo, nullptr, &depthImageView_),
        "vkCreateImageView");
    context_.setDebugObjectName(
        VK_OBJECT_TYPE_IMAGE_VIEW,
        reinterpret_cast<uint64_t>(depthImageView_),
        "Rubber duck depth image view");
}

void RubberDuckCommandRecorder::destroyDepthAttachment() {
    if (depthImageView_) {
        vkDestroyImageView(context_.device(), depthImageView_, nullptr);
        depthImageView_ = VK_NULL_HANDLE;
    }

    if (depthImage_ && depthAllocation_) {
        vmaDestroyImage(context_.allocator(), depthImage_, depthAllocation_);
        depthImage_ = VK_NULL_HANDLE;
        depthAllocation_ = nullptr;
    }
}
