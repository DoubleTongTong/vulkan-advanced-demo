#include "rendering/ComputedMeshCommandRecorder.h"

#include "VulkanContext.h"
#include "VulkanProfiler.h"
#include "VulkanShaderModule.h"
#include "VulkanSwapchain.h"
#include "VulkanUtils.h"

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <stdexcept>

namespace {

constexpr const char* VerticesResource = "kVertices";
constexpr const char* OutputImageResource = "kOutputImage";
constexpr const char* GeneratedTextureResource = "kGeneratedTexture";
constexpr const char* LinearSamplerResource = "kLinearSampler";

struct alignas(16) MeshPushConstants {
    // x/y = 网格尺寸，z/w = 当前 P/Q。
    glm::uvec4 gridAndCurrent{0};
    // x/y = 目标 P/Q。
    glm::uvec4 nextKnot{0};
    // x = 时间，y = 经过平滑后的 Morph 系数。
    glm::vec4 animation{0.0f};
};

struct alignas(16) RenderPushConstants {
    glm::mat4 mvp{1.0f};
    // x > 0 表示使用程序化彩色材质，否则采样 Compute 生成的纹理。
    glm::vec4 options{0.0f};
};

static_assert(sizeof(MeshPushConstants) == 48);
static_assert(sizeof(RenderPushConstants) == 80);

float smoothMorph(float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

ComputedMeshCommandRecorder::ComputedMeshCommandRecorder(
    const VulkanContext& context,
    const VulkanSwapchain& swapchain,
    const VulkanShaderModule& meshComputeShader,
    const VulkanShaderModule& textureComputeShader,
    const VulkanShaderModule& vertexShader,
    const VulkanShaderModule& geometryShader,
    const VulkanShaderModule& fragmentShader)
    : context_(context),
      swapchain_(swapchain),
      mesh_(context, NumU, NumV),
      texture_(
          context,
          TextureExtent,
          "Computed mesh texture",
          VK_SAMPLER_ADDRESS_MODE_REPEAT),
      meshWriteDescriptors_(
          context,
          DescriptorSetDesc{
              .shaders = {&meshComputeShader},
              .debugName = "Computed mesh vertex writer",
          }),
      textureWriteDescriptors_(
          context,
          DescriptorSetDesc{
              .shaders = {&textureComputeShader},
              .debugName = "Computed mesh texture writer",
          }),
      textureReadDescriptors_(
          context,
          DescriptorSetDesc{
              .shaders = {&fragmentShader},
              .debugName = "Computed mesh texture reader",
          }),
      meshComputePipeline_(
          context,
          ComputePipelineDesc{
              .shader = &meshComputeShader,
              .descriptorSetLayouts = {meshWriteDescriptors_.layout()},
              .debugName = "Torus knot generation pipeline",
          }),
      textureComputePipeline_(
          context,
          ComputePipelineDesc{
              .shader = &textureComputeShader,
              .descriptorSetLayouts = {textureWriteDescriptors_.layout()},
              .debugName = "Torus knot texture pipeline",
          }),
      renderPipeline_(
          context,
          RenderPipelineDesc{
              .vertexShader = &vertexShader,
              .geometryShader = &geometryShader,
              .fragmentShader = &fragmentShader,
              .vertexBindings = VulkanComputedMesh::vertexBindings(),
              .vertexAttributes = VulkanComputedMesh::vertexAttributes(),
              .colorFormat = swapchain.imageFormat(),
              .depthFormat = VulkanDepthAttachment::DefaultFormat,
              .cullMode = VK_CULL_MODE_NONE,
              .depthTestEnabled = true,
              .depthWriteEnabled = true,
              .depthCompareOp = VK_COMPARE_OP_LESS,
              .descriptorSetLayouts = {textureReadDescriptors_.layout()},
              .debugName = "Computed torus knot render pipeline",
          }),
      depthAttachment_(context, swapchain.extent(), "Computed mesh depth"),
      imageLayouts_(swapchain.images().size(), VK_IMAGE_LAYOUT_UNDEFINED),
      startTime_(std::chrono::steady_clock::now()),
      previousFrameTime_(startTime_) {
    if ((swapchain.imageUsage() & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
        throw std::runtime_error("Swapchain images do not support color attachment usage.");
    }

    meshWriteDescriptors_.writeStorageBuffer(VerticesResource, mesh_.vertexBuffer());
    textureWriteDescriptors_.writeStorageImage2D(OutputImageResource, 0, texture_);
    textureReadDescriptors_.writeTexture2D(GeneratedTextureResource, 0, texture_);
    textureReadDescriptors_.writeSampler(LinearSamplerResource, 0, texture_.sampler());
}

void ComputedMeshCommandRecorder::record(const RenderFrameContext& frame) {
    APP_PROFILE_FUNCTION();
    APP_PROFILE_GPU_ZONE(context_, frame.commandBuffer, "Computed torus knot");

    const auto now = std::chrono::steady_clock::now();
    const float seconds = std::chrono::duration<float>(now - startTime_).count();
    const float deltaSeconds = std::chrono::duration<float>(now - previousFrameTime_).count();
    previousFrameTime_ = now;
    advanceMorph(deltaSeconds);

    const TorusKnotParameters current = morphQueue_[0];
    const TorusKnotParameters next = morphQueue_[1];
    const MeshPushConstants meshPush{
        .gridAndCurrent = {mesh_.uSegments(), mesh_.vSegments(), current.p, current.q},
        .nextKnot = {next.p, next.q, 0, 0},
        .animation = {seconds, smoothMorph(morphCoefficient_), 0.0f, 0.0f},
    };

    const VkCommandBuffer commandBuffer = frame.commandBuffer;
    mesh_.transitionForComputeWrite(commandBuffer);
    meshComputePipeline_.bind(commandBuffer);
    meshWriteDescriptors_.bindCompute(commandBuffer, meshComputePipeline_.layout());
    vkCmdPushConstants(
        commandBuffer,
        meshComputePipeline_.layout(),
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(meshPush),
        &meshPush);
    const uint32_t meshGroups =
        (mesh_.vertexCount() + MeshLocalSize - 1) / MeshLocalSize;
    vkCmdDispatch(commandBuffer, meshGroups, 1, 1);
    mesh_.transitionForVertexRead(commandBuffer);

    // 彩色模式完全不需要纹理，跳过第二条 Compute Pipeline。
    if (!useColoredMesh_) {
        texture_.transitionForComputeWrite(commandBuffer);
        textureComputePipeline_.bind(commandBuffer);
        textureWriteDescriptors_.bindCompute(commandBuffer, textureComputePipeline_.layout());
        vkCmdPushConstants(
            commandBuffer,
            textureComputePipeline_.layout(),
            VK_SHADER_STAGE_COMPUTE_BIT,
            0,
            sizeof(seconds),
            &seconds);
        const uint32_t groupCountX = (TextureExtent.width + 15) / 16;
        const uint32_t groupCountY = (TextureExtent.height + 15) / 16;
        vkCmdDispatch(commandBuffer, groupCountX, groupCountY, 1);
        texture_.transitionForSampling(commandBuffer);
    }

    const uint32_t imageIndex = frame.imageIndex;
    const VkImage image = swapchain_.images().at(imageIndex);
    const VkExtent2D extent = swapchain_.extent();
    vulkan_utils::transitionImage(
        commandBuffer,
        image,
        VK_IMAGE_ASPECT_COLOR_BIT,
        imageLayouts_.at(imageIndex),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        0,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    depthAttachment_.transitionForWrite(commandBuffer);

    const VkClearValue clearColor{.color = {{0.025f, 0.03f, 0.045f, 1.0f}}};
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

    const RenderPushConstants renderPush{
        .mvp = frame.view.viewProjection,
        .options = {useColoredMesh_ ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f},
    };
    renderPipeline_.bind(commandBuffer);
    textureReadDescriptors_.bind(commandBuffer, renderPipeline_.layout());
    vkCmdPushConstants(
        commandBuffer,
        renderPipeline_.layout(),
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(renderPush),
        &renderPush);
    mesh_.bindAndDraw(commandBuffer);
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

void ComputedMeshCommandRecorder::requestKnot(TorusKnotParameters knot) {
    if (knot.p == 0 || knot.q == 0 || knot == morphQueue_.back()) {
        return;
    }

    // 空闲时直接替换“下一个”结，点击后无需先等待一段无变化动画。
    if (morphQueue_.size() == 2 &&
        morphQueue_[0] == morphQueue_[1] &&
        morphCoefficient_ == 0.0f) {
        morphQueue_[1] = knot;
    } else {
        morphQueue_.push_back(knot);
    }
}

const std::deque<TorusKnotParameters>& ComputedMeshCommandRecorder::morphQueue() const {
    return morphQueue_;
}

float ComputedMeshCommandRecorder::animationSpeed() const {
    return animationSpeed_;
}

void ComputedMeshCommandRecorder::setAnimationSpeed(float speed) {
    animationSpeed_ = std::clamp(speed, 0.0f, 2.0f);
}

bool ComputedMeshCommandRecorder::useColoredMesh() const {
    return useColoredMesh_;
}

void ComputedMeshCommandRecorder::setUseColoredMesh(bool colored) {
    useColoredMesh_ = colored;
}

float ComputedMeshCommandRecorder::morphCoefficient() const {
    return morphCoefficient_;
}

const VulkanStorageTexture2D& ComputedMeshCommandRecorder::generatedTexture() const {
    return texture_;
}

void ComputedMeshCommandRecorder::advanceMorph(float deltaSeconds) {
    if (morphQueue_[0] == morphQueue_[1] || animationSpeed_ <= 0.0f) {
        return;
    }

    morphCoefficient_ += deltaSeconds * animationSpeed_;
    while (morphCoefficient_ >= 1.0f) {
        morphCoefficient_ -= 1.0f;
        if (morphQueue_.size() > 2) {
            morphQueue_.pop_front();
        } else {
            morphQueue_.front() = morphQueue_.back();
            morphCoefficient_ = 0.0f;
            break;
        }
    }
}
