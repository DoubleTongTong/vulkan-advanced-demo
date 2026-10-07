#include "rendering/ComputeTextureCommandRecorder.h"

#include "VulkanContext.h"
#include "VulkanProfiler.h"
#include "VulkanShaderModule.h"
#include "VulkanSwapchain.h"
#include "VulkanUtils.h"

#include <stdexcept>

namespace {

constexpr const char* OutputImageResource = "kOutputImage";
constexpr const char* GeneratedTextureResource = "kGeneratedTexture";
constexpr const char* LinearSamplerResource = "kLinearSampler";

} // namespace

ComputeTextureCommandRecorder::ComputeTextureCommandRecorder(
    const VulkanContext& context,
    const VulkanSwapchain& swapchain,
    const VulkanShaderModule& computeShader,
    const VulkanShaderModule& vertexShader,
    const VulkanShaderModule& fragmentShader)
    : context_(context),
      swapchain_(swapchain),
      texture_(context, swapchain.extent(), "Compute-generated texture"),
      descriptors_(
          context,
          DescriptorSetDesc{
              .shaders = {&computeShader, &fragmentShader},
              .debugName = "Compute texture descriptors",
          }),
      computePipeline_(
          context,
          ComputePipelineDesc{
              .shader = &computeShader,
              .descriptorSetLayouts = {descriptors_.layout()},
              .debugName = "Procedural texture compute pipeline",
          }),
      renderPipeline_(
          context,
          RenderPipelineDesc{
              .vertexShader = &vertexShader,
              .fragmentShader = &fragmentShader,
              .colorFormat = swapchain.imageFormat(),
              .descriptorSetLayouts = {descriptors_.layout()},
              .debugName = "Compute texture fullscreen pipeline",
          }),
      startTime_(std::chrono::steady_clock::now()),
      imageLayouts_(swapchain.images().size(), VK_IMAGE_LAYOUT_UNDEFINED) {
    if ((swapchain.imageUsage() & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
        throw std::runtime_error("Swapchain images do not support color attachment usage.");
    }

    // 同一个 ImageView 在 Compute 阶段按 GENERAL 写入，在 Fragment 阶段按
    // SHADER_READ_ONLY_OPTIMAL 采样；两个 Descriptor 各自声明访问时的布局。
    descriptors_.writeStorageImage2D(OutputImageResource, 0, texture_);
    descriptors_.writeTexture2D(GeneratedTextureResource, 0, texture_);
    descriptors_.writeSampler(LinearSamplerResource, 0, texture_.sampler());
}

void ComputeTextureCommandRecorder::record(const RenderFrameContext& frame) {
    APP_PROFILE_FUNCTION();
    APP_PROFILE_GPU_ZONE(context_, frame.commandBuffer, "Compute-generated texture");

    const VkCommandBuffer commandBuffer = frame.commandBuffer;
    const float seconds = std::chrono::duration<float>(
        std::chrono::steady_clock::now() - startTime_).count();

    // 先把纹理交给 Compute Shader。首帧从 UNDEFINED 转换，后续帧则等待
    // 上一帧 Fragment Shader 的采样读取结束。
    texture_.transitionForComputeWrite(commandBuffer);
    computePipeline_.bind(commandBuffer);
    descriptors_.bindCompute(commandBuffer, computePipeline_.layout());
    vkCmdPushConstants(
        commandBuffer,
        computePipeline_.layout(),
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(seconds),
        &seconds);

    const VkExtent2D textureExtent = texture_.extent();
    const uint32_t groupCountX = (textureExtent.width + LocalSize - 1) / LocalSize;
    const uint32_t groupCountY = (textureExtent.height + LocalSize - 1) / LocalSize;
    vkCmdDispatch(commandBuffer, groupCountX, groupCountY, 1);

    // 这次转换同时是 Compute Write -> Fragment Read 的内存屏障。
    texture_.transitionForSampling(commandBuffer);

    const uint32_t imageIndex = frame.imageIndex;
    const VkImage swapchainImage = swapchain_.images().at(imageIndex);
    const VkExtent2D extent = swapchain_.extent();
    vulkan_utils::transitionImage(
        commandBuffer,
        swapchainImage,
        VK_IMAGE_ASPECT_COLOR_BIT,
        imageLayouts_.at(imageIndex),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        0,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    const VkClearValue clearColor{.color = {{0.0f, 0.0f, 0.0f, 1.0f}}};
    const VkRenderingAttachmentInfo colorAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = swapchain_.imageViews().at(imageIndex),
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = clearColor,
    };
    const VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.offset = {0, 0}, .extent = extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
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
    const VkRect2D scissor{.offset = {0, 0}, .extent = extent};
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    renderPipeline_.bind(commandBuffer);
    descriptors_.bind(commandBuffer, renderPipeline_.layout());
    // 顶点着色器用 gl_VertexIndex 生成覆盖屏幕的一个大三角形。
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    vkCmdEndRendering(commandBuffer);

    vulkan_utils::transitionImage(
        commandBuffer,
        swapchainImage,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        0,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    imageLayouts_[imageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
}
