#include "rendering/MetallicRoughnessGltfCommandRecorder.h"

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

constexpr const char* MaterialSampler = "kMaterialSampler";
constexpr const char* IblSampler = "kIblSampler";
constexpr const char* BaseColorTexture = "kBaseColorTexture";
constexpr const char* MetallicRoughnessTexture = "kMetallicRoughnessTexture";
constexpr const char* NormalTexture = "kNormalTexture";
constexpr const char* OcclusionTexture = "kOcclusionTexture";
constexpr const char* EmissiveTexture = "kEmissiveTexture";
constexpr const char* BrdfLut = "kBrdfLut";
constexpr const char* IrradianceMap = "kIrradianceMap";
constexpr const char* GgxEnvironment = "kGgxEnvironment";
constexpr const char* FrameDataResource = "kFrameData";

struct alignas(16) FrameData {
    glm::mat4 model{1.0f};
    glm::mat4 mvp{1.0f};
    glm::vec4 cameraPosition{0.0f};
    glm::vec4 baseColorFactor{1.0f};
    glm::vec4 emissiveFactor{0.0f};
    // x=metallic, y=roughness, z=normalScale, w=occlusionStrength
    glm::vec4 materialFactors{1.0f};
};

static_assert(sizeof(FrameData) == 192);

} // namespace

MetallicRoughnessGltfCommandRecorder::MetallicRoughnessGltfCommandRecorder(
    const VulkanContext& context,
    const VulkanSwapchain& swapchain,
    const VulkanShaderModule& vertexShader,
    const VulkanShaderModule& fragmentShader,
    const std::filesystem::path& scenePath,
    const std::filesystem::path& brdfLutPath,
    const std::filesystem::path& irradiancePath,
    const std::filesystem::path& ggxEnvironmentPath)
    : context_(context),
      swapchain_(swapchain),
      meshData_(GeometryCache::loadOrConvert(scenePath)),
      mesh_(context, meshData_),
      material_(scenePath),
      baseColorTexture_(
          context,
          ImageProcessor().loadRgba8(material_.baseColorTexturePath()),
          "PBR base color",
          VK_FORMAT_R8G8B8A8_SRGB),
      metallicRoughnessTexture_(
          context,
          ImageProcessor().loadRgba8(material_.metallicRoughnessTexturePath()),
          "PBR metallic roughness"),
      normalTexture_(
          context,
          ImageProcessor().loadRgba8(material_.normalTexturePath()),
          "PBR normal"),
      occlusionTexture_(
          context,
          ImageProcessor().loadRgba8(material_.occlusionTexturePath()),
          "PBR occlusion"),
      emissiveTexture_(
          context,
          ImageProcessor().loadRgba8(material_.emissiveTexturePath()),
          "PBR emissive",
          VK_FORMAT_R8G8B8A8_SRGB),
      brdfLut_(context, brdfLutPath, "PBR BRDF LUT"),
      irradianceMap_(context, irradiancePath, "PBR irradiance map"),
      ggxEnvironment_(context, ggxEnvironmentPath, "PBR GGX environment"),
      frameDataBuffer_(
          context,
          BufferDesc{
              .usage = BufferUsage_Storage,
              .storage = BufferStorage::HostVisible,
              // 每个 in-flight frame 使用独立区间，避免 CPU 覆盖 GPU 仍在读取的数据。
              .size = sizeof(FrameData) * swapchain.images().size(),
              .debugName = "PBR frame and material data",
          }),
      descriptors_(
          context,
          DescriptorSetDesc{
              .shaders = {&vertexShader, &fragmentShader},
              .debugName = "glTF metallic-roughness descriptors",
          }),
      pipeline_(
          context,
          RenderPipelineDesc{
              .vertexShader = &vertexShader,
              .fragmentShader = &fragmentShader,
              .vertexBindings = {{0, sizeof(MeshVertex), VK_VERTEX_INPUT_RATE_VERTEX}},
              .vertexAttributes = {
                  {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, position)},
                  {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, normal)},
                  {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(MeshVertex, uv)},
              },
              .colorFormat = swapchain.imageFormat(),
              .depthFormat = VulkanDepthAttachment::DefaultFormat,
              .cullMode = material_.doubleSided()
                  ? VkCullModeFlags{VK_CULL_MODE_NONE}
                  : VkCullModeFlags{VK_CULL_MODE_BACK_BIT},
              .depthTestEnabled = true,
              .depthWriteEnabled = true,
              .depthCompareOp = VK_COMPARE_OP_LESS,
              .descriptorSetLayouts = {descriptors_.layout()},
              .debugName = "glTF metallic-roughness pipeline",
          }),
      depthAttachment_(context, swapchain.extent(), "glTF metallic-roughness depth"),
      imageLayouts_(swapchain.images().size(), VK_IMAGE_LAYOUT_UNDEFINED) {
    if (brdfLut_.isCube() || !irradianceMap_.isCube() || !ggxEnvironment_.isCube()) {
        throw std::runtime_error("PBR IBL KTX2 resources have incompatible texture dimensions.");
    }

    descriptors_.writeSampler(MaterialSampler, 0, baseColorTexture_.sampler());
    descriptors_.writeSampler(IblSampler, 0, ggxEnvironment_.sampler());
    descriptors_.writeTexture2D(BaseColorTexture, 0, baseColorTexture_);
    descriptors_.writeTexture2D(MetallicRoughnessTexture, 0, metallicRoughnessTexture_);
    descriptors_.writeTexture2D(NormalTexture, 0, normalTexture_);
    descriptors_.writeTexture2D(OcclusionTexture, 0, occlusionTexture_);
    descriptors_.writeTexture2D(EmissiveTexture, 0, emissiveTexture_);
    descriptors_.writeKtxTexture2D(BrdfLut, 0, brdfLut_);
    descriptors_.writeKtxTextureCube(IrradianceMap, 0, irradianceMap_);
    descriptors_.writeKtxTextureCube(GgxEnvironment, 0, ggxEnvironment_);
    descriptors_.writeStorageBuffer(FrameDataResource, frameDataBuffer_);
}

void MetallicRoughnessGltfCommandRecorder::record(const RenderFrameContext& frame) {
    APP_PROFILE_FUNCTION();
    APP_PROFILE_GPU_ZONE(context_, frame.commandBuffer, "glTF metallic-roughness");

    using Clock = std::chrono::steady_clock;
    static const Clock::time_point startTime = Clock::now();
    const float seconds = std::chrono::duration<float>(Clock::now() - startTime).count();
    const MeshBounds& bounds = meshData_.bounds();
    const glm::vec3 center(bounds.center[0], bounds.center[1], bounds.center[2]);
    const glm::mat4 model =
        glm::rotate(glm::mat4(1.0f), seconds * 0.25f, glm::vec3(0.0f, 1.0f, 0.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(1.0f / bounds.radius)) *
        glm::translate(glm::mat4(1.0f), -center);
    const FrameData frameData{
        .model = model,
        .mvp = frame.view.viewProjection * model,
        .cameraPosition = glm::vec4(frame.view.cameraPosition, 1.0f),
        .baseColorFactor = material_.baseColorFactor(),
        .emissiveFactor = glm::vec4(material_.emissiveFactor(), 1.0f),
        .materialFactors = {
            material_.metallicFactor(),
            material_.roughnessFactor(),
            material_.normalScale(),
            material_.occlusionStrength(),
        },
    };
    frameDataBuffer_.bufferSubData(
        sizeof(FrameData) * frame.frameIndex,
        sizeof(frameData),
        &frameData);

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

    const VkClearValue clearColor{.color = {{0.025f, 0.03f, 0.04f, 1.0f}}};
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

    pipeline_.bind(commandBuffer);
    descriptors_.bind(commandBuffer, pipeline_.layout());
    vkCmdPushConstants(
        commandBuffer,
        pipeline_.layout(),
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(frame.frameIndex),
        &frame.frameIndex);
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
