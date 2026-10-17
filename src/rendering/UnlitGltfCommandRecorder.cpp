#include "rendering/UnlitGltfCommandRecorder.h"

#include "ImageProcessor.h"
#include "VulkanContext.h"
#include "VulkanProfiler.h"
#include "VulkanShaderModule.h"
#include "VulkanSwapchain.h"
#include "VulkanUtils.h"
#include "mesh/GeometryCache.h"

#include <glm/ext/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <chrono>
#include <cstddef>
#include <stdexcept>

namespace {

constexpr const char* BaseColorTextureResource = "kBaseColorTexture";
constexpr const char* BaseColorSamplerResource = "kBaseColorSampler";

struct alignas(16) PushConstants {
    glm::mat4 mvp{1.0f};
    glm::vec4 baseColor{1.0f};
};

static_assert(sizeof(PushConstants) == 80);

} // namespace

UnlitGltfCommandRecorder::UnlitGltfCommandRecorder(
    const VulkanContext& context,
    const VulkanSwapchain& swapchain,
    const VulkanShaderModule& vertexShader,
    const VulkanShaderModule& fragmentShader,
    const std::filesystem::path& scenePath)
    : context_(context),
      swapchain_(swapchain),
      meshData_(GeometryCache::loadOrConvert(scenePath)),
      mesh_(context, meshData_),
      material_(scenePath),
      // glTF 的 baseColorTexture 是 sRGB；采样时让硬件自动解码到线性空间。
      baseColorTexture_(
          context,
          ImageProcessor().loadRgba8(material_.baseColorTexturePath()),
          "glTF unlit base color",
          VK_FORMAT_R8G8B8A8_SRGB),
      materialDescriptors_(
          context,
          DescriptorSetDesc{
              .shaders = {&fragmentShader},
              .debugName = "glTF unlit material descriptors",
          }),
      pipeline_(
          context,
          RenderPipelineDesc{
              .vertexShader = &vertexShader,
              .fragmentShader = &fragmentShader,
              .vertexBindings = {
                  {0, sizeof(MeshVertex), VK_VERTEX_INPUT_RATE_VERTEX},
              },
              .vertexAttributes = {
                  {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, position)},
                  {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(MeshVertex, uv)},
              },
              .colorFormat = swapchain.imageFormat(),
              .depthFormat = VulkanDepthAttachment::DefaultFormat,
              .cullMode = VK_CULL_MODE_BACK_BIT,
              .depthTestEnabled = true,
              .depthWriteEnabled = true,
              .depthCompareOp = VK_COMPARE_OP_LESS,
              .descriptorSetLayouts = {materialDescriptors_.layout()},
              .debugName = "glTF unlit pipeline",
          }),
      depthAttachment_(context, swapchain.extent(), "glTF unlit depth"),
      imageLayouts_(swapchain.images().size(), VK_IMAGE_LAYOUT_UNDEFINED) {
    if ((swapchain.imageUsage() & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
        throw std::runtime_error("Swapchain images do not support color attachment usage.");
    }

    materialDescriptors_.writeTexture2D(BaseColorTextureResource, 0, baseColorTexture_);
    materialDescriptors_.writeSampler(
        BaseColorSamplerResource, 0, baseColorTexture_.sampler());
}

void UnlitGltfCommandRecorder::record(const RenderFrameContext& frame) {
    APP_PROFILE_FUNCTION();
    APP_PROFILE_GPU_ZONE(context_, frame.commandBuffer, "glTF unlit");

    const VkCommandBuffer commandBuffer = frame.commandBuffer;
    const uint32_t imageIndex = frame.imageIndex;
    const VkExtent2D extent = swapchain_.extent();
    vulkan_utils::transitionImage(
        commandBuffer,
        swapchain_.images().at(imageIndex),
        VK_IMAGE_ASPECT_COLOR_BIT,
        imageLayouts_.at(imageIndex),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        0,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    depthAttachment_.transitionForWrite(commandBuffer);

    const VkClearValue clearColor{.color = {{0.055f, 0.065f, 0.085f, 1.0f}}};
    const VkRenderingAttachmentInfo colorAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = swapchain_.imageViews().at(imageIndex),
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = clearColor,
    };
    const VkRenderingAttachmentInfo depthAttachment = depthAttachment_.renderingInfo();
    const VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, extent},
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
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    using Clock = std::chrono::steady_clock;
    static const Clock::time_point startTime = Clock::now();
    const float seconds = std::chrono::duration<float>(Clock::now() - startTime).count();
    const MeshBounds& bounds = meshData_.bounds();
    const glm::vec3 center(bounds.center[0], bounds.center[1], bounds.center[2]);
    const glm::mat4 model =
        glm::rotate(glm::mat4(1.0f), seconds * 0.35f, glm::vec3(0.0f, 1.0f, 0.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(1.0f / bounds.radius)) *
        glm::translate(glm::mat4(1.0f), -center);
    const PushConstants pushConstants{
        .mvp = frame.view.viewProjection * model,
        .baseColor = material_.baseColorFactor(),
    };

    pipeline_.bind(commandBuffer);
    materialDescriptors_.bind(commandBuffer, pipeline_.layout());
    vkCmdPushConstants(
        commandBuffer,
        pipeline_.layout(),
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(pushConstants),
        &pushConstants);
    mesh_.bindAndDraw(commandBuffer);
    vkCmdEndRendering(commandBuffer);

    vulkan_utils::transitionImage(
        commandBuffer,
        swapchain_.images().at(imageIndex),
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        0,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    imageLayouts_[imageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
}
