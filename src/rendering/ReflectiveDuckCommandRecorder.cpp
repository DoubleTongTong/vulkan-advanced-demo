#include "rendering/ReflectiveDuckCommandRecorder.h"

#include "CubeMapProcessor.h"
#include "ImageProcessor.h"
#include "mesh/ModelLoader.h"
#include "VulkanContext.h"
#include "VulkanProfiler.h"
#include "VulkanShaderModule.h"
#include "VulkanSwapchain.h"
#include "VulkanUtils.h"

#include <glm/ext/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <vk_mem_alloc.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace {

constexpr VkFormat DepthFormat = VK_FORMAT_D32_SFLOAT;
constexpr uint32_t MaxBindlessTextures = 16;
constexpr const char* SamplersResource = "kSamplers";
constexpr const char* CubeTexturesResource = "kTexturesCube";
constexpr const char* Textures2DResource = "kTextures2D";

struct Transform {
    glm::mat4 mvp{1.0f};
    glm::mat4 inverseViewProjection{1.0f};
    float rotationAngle = 0.0f;
};

Transform makeTransform(
    const float center[3],
    float radius,
    const RenderView& view) {
    using Clock = std::chrono::steady_clock;
    static const Clock::time_point startTime = Clock::now();

    const float seconds = std::chrono::duration<float>(Clock::now() - startTime).count();
    const float rotationAngle = glm::radians(35.0f) + seconds;
    const glm::mat4 model =
        glm::rotate(glm::mat4(1.0f), rotationAngle, glm::vec3(0.0f, 1.0f, 0.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(1.0f / radius)) *
        glm::translate(glm::mat4(1.0f), glm::vec3(-center[0], -center[1], -center[2]));
    return {
        .mvp = view.viewProjection * model,
        .inverseViewProjection = view.inverseViewProjection,
        .rotationAngle = rotationAngle,
    };
}

} // namespace

ReflectiveDuckCommandRecorder::ReflectiveDuckCommandRecorder(
    const VulkanContext& context,
    const VulkanSwapchain& swapchain,
    const VulkanShaderModule& vertexShader,
    const VulkanShaderModule& fragmentShader,
    const VulkanShaderModule& skyVertexShader,
    const VulkanShaderModule& skyFragmentShader,
    const std::filesystem::path& scenePath,
    const std::filesystem::path& texturePath,
    const std::filesystem::path& environmentPath)
    : context_(context),
      swapchain_(swapchain),
      lineCanvas_(context, swapchain),
      sceneData_(loadSceneData(scenePath)),
      texture_(context, ImageProcessor().loadRgba8(texturePath), "Reflective duck base color texture"),
      environment_(context, loadEnvironment(environmentPath), "Piazza Bologni environment cube"),
      textureDescriptors_(
          context,
          {
              .shaders = {
                  &vertexShader,
                  &fragmentShader,
                  &skyVertexShader,
                  &skyFragmentShader,
              },
              .runtimeArrays = {{Textures2DResource, MaxBindlessTextures}},
              .debugName = "Reflective duck bindless descriptors",
          }),
      vertexBuffer_(
          context,
          {
              .usage = BufferUsage_Vertex,
              .storage = BufferStorage::Device,
              .size = sizeof(Vertex) * sceneData_.vertices.size(),
              .data = sceneData_.vertices.data(),
              .debugName = "Reflective duck vertex buffer",
          }),
      indexBuffer_(
          context,
          {
              .usage = BufferUsage_Index,
              .storage = BufferStorage::Device,
              .size = sizeof(uint32_t) * sceneData_.indices.size(),
              .data = sceneData_.indices.data(),
              .debugName = "Reflective duck index buffer",
          }),
      imageLayouts_(swapchain.images().size(), VK_IMAGE_LAYOUT_UNDEFINED) {
    meshCenter_[0] = sceneData_.center[0];
    meshCenter_[1] = sceneData_.center[1];
    meshCenter_[2] = sceneData_.center[2];
    meshRadius_ = sceneData_.radius;
    indexCount_ = static_cast<uint32_t>(sceneData_.indices.size());

    if ((swapchain.imageUsage() & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
        throw std::runtime_error("Swapchain images do not support color attachment usage.");
    }

    textureDescriptors_.fillSamplers(SamplersResource, texture_.sampler());
    textureDescriptors_.writeSampler(SamplersResource, 1, environment_.sampler());
    textureDescriptors_.writeTexture2D(Textures2DResource, 0, texture_);
    textureDescriptors_.writeTextureCube(CubeTexturesResource, 0, environment_);
    createDepthAttachment();
    createPipelines(vertexShader, fragmentShader, skyVertexShader, skyFragmentShader);
}

ReflectiveDuckCommandRecorder::~ReflectiveDuckCommandRecorder() {
    duckPipeline_.reset();
    skyPipeline_.reset();
    destroyDepthAttachment();
}

ReflectiveDuckCommandRecorder::SceneData ReflectiveDuckCommandRecorder::loadSceneData(
    const std::filesystem::path& scenePath) {
    const MeshData mesh = ModelLoader::loadFirstMesh(scenePath);
    const size_t vertexCount = mesh.positions.size() / 3u;
    if (vertexCount != mesh.normals.size() / 3u || vertexCount != mesh.texcoords.size() / 2u) {
        throw std::runtime_error("Model vertex attributes do not match: " + scenePath.string());
    }

    SceneData data;
    data.vertices.resize(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i) {
        for (size_t component = 0; component < 3; ++component) {
            data.vertices[i].position[component] = mesh.positions[i * 3u + component];
            data.vertices[i].normal[component] = mesh.normals[i * 3u + component];
        }
        data.vertices[i].uv[0] = mesh.texcoords[i * 2u + 0u];
        data.vertices[i].uv[1] = 1.0f - mesh.texcoords[i * 2u + 1u];
    }

    data.indices = mesh.indices;
    data.center[0] = mesh.center[0];
    data.center[1] = mesh.center[1];
    data.center[2] = mesh.center[2];
    data.radius = mesh.radius;
    return data;
}

CubeMapImage ReflectiveDuckCommandRecorder::loadEnvironment(
    const std::filesystem::path& environmentPath) {
    const ImageProcessor imageProcessor;
    const RgbaFloatImage equirectangular = imageProcessor.loadRgba32Float(environmentPath);
    const CubeMapImage cubeMap = CubeMapProcessor().fromEquirectangular(equirectangular);

    const std::filesystem::path outputDirectory = "debug-output/cubemap";
    imageProcessor.saveHdr(
        outputDirectory / "equirectangular-rgba32f.hdr",
        equirectangular);

    constexpr std::array<const char*, 6> FaceNames = {
        "positive-x.hdr",
        "negative-x.hdr",
        "positive-y.hdr",
        "negative-y.hdr",
        "positive-z.hdr",
        "negative-z.hdr",
    };
    const size_t faceFloatCount =
        static_cast<size_t>(cubeMap.faceSize) * cubeMap.faceSize * 4u;
    for (size_t face = 0; face < FaceNames.size(); ++face) {
        const auto first = cubeMap.pixels.begin() + faceFloatCount * face;
        RgbaFloatImage faceImage;
        faceImage.width = cubeMap.faceSize;
        faceImage.height = cubeMap.faceSize;
        faceImage.pixels.assign(first, first + faceFloatCount);
        imageProcessor.saveHdr(outputDirectory / FaceNames[face], faceImage);
    }

    return cubeMap;
}

void ReflectiveDuckCommandRecorder::createPipelines(
    const VulkanShaderModule& vertexShader,
    const VulkanShaderModule& fragmentShader,
    const VulkanShaderModule& skyVertexShader,
    const VulkanShaderModule& skyFragmentShader) {
    duckPipeline_ = std::make_unique<VulkanRenderPipeline>(
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
                    .format = VK_FORMAT_R32G32B32_SFLOAT,
                    .offset = offsetof(Vertex, position),
                },
                {
                    .location = 1,
                    .binding = 0,
                    .format = VK_FORMAT_R32G32B32_SFLOAT,
                    .offset = offsetof(Vertex, normal),
                },
                {
                    .location = 2,
                    .binding = 0,
                    .format = VK_FORMAT_R32G32_SFLOAT,
                    .offset = offsetof(Vertex, uv),
                },
            },
            .colorFormat = swapchain_.imageFormat(),
            .depthFormat = DepthFormat,
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .depthTestEnabled = true,
            .depthWriteEnabled = true,
            .descriptorSetLayouts = {textureDescriptors_.layout()},
            .debugName = "Reflective duck pipeline",
        });

    // 天空只画一个全屏三角形，不需要 vertex buffer，也不参与深度测试。
    skyPipeline_ = std::make_unique<VulkanRenderPipeline>(
        context_,
        RenderPipelineDesc{
            .vertexShader = &skyVertexShader,
            .fragmentShader = &skyFragmentShader,
            .colorFormat = swapchain_.imageFormat(),
            .depthFormat = DepthFormat,
            .descriptorSetLayouts = {textureDescriptors_.layout()},
            .debugName = "Cube map sky pipeline",
        });
}

void ReflectiveDuckCommandRecorder::buildDebugCanvas() {
    // 这些辅助图元属于当前鸭子示例，而不是应用主循环的职责。
    lineCanvas_.clear();
    lineCanvas_.plane(
        {0.0f, -1.05f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        12, 12, 6.0f, 6.0f,
        {0.25f, 0.35f, 0.45f, 0.55f},
        {0.55f, 0.70f, 0.90f, 0.9f});
    lineCanvas_.box(glm::mat4(1.0f), {1.0f, 1.0f, 1.0f}, {1.0f, 0.75f, 0.15f, 1.0f});
    lineCanvas_.line({0.0f, 0.0f, 0.0f}, {1.5f, 0.0f, 0.0f}, {1.0f, 0.1f, 0.1f, 1.0f});
    lineCanvas_.line({0.0f, 0.0f, 0.0f}, {0.0f, 1.5f, 0.0f}, {0.1f, 1.0f, 0.1f, 1.0f});
    lineCanvas_.line({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.5f}, {0.1f, 0.4f, 1.0f, 1.0f});
}

void ReflectiveDuckCommandRecorder::record(const RenderFrameContext& frame) {
    const VkCommandBuffer commandBuffer = frame.commandBuffer;
    const uint32_t imageIndex = frame.imageIndex;
    APP_PROFILE_FUNCTION();
    APP_PROFILE_GPU_ZONE(context_, commandBuffer, "Reflective duck");
    const VkImage image = swapchain_.images()[imageIndex];
    const VkExtent2D extent = swapchain_.extent();

    vulkan_utils::transitionImage(
        commandBuffer, image, VK_IMAGE_ASPECT_COLOR_BIT,
        imageLayouts_[imageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    vulkan_utils::transitionImage(
        commandBuffer, depthImage_, VK_IMAGE_ASPECT_DEPTH_BIT,
        depthLayout_, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);

    const VkClearValue clearColor{.color = {{0.32f, 0.34f, 0.36f, 1.0f}}};
    const VkClearValue clearDepth{.depthStencil = {1.0f, 0}};
    const VkRenderingAttachmentInfo colorAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = swapchain_.imageViews()[imageIndex],
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
        .renderArea = {.offset = {0, 0}, .extent = extent},
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
    const VkRect2D scissor{.offset = {0, 0}, .extent = extent};
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    const Transform transform = makeTransform(meshCenter_, meshRadius_, frame.view);
    const glm::vec3 cameraPosition = frame.view.cameraPosition;
    struct SkyPushConstants {
        glm::mat4 inverseViewProjection;
        glm::vec4 cameraPosition;
    };
    const SkyPushConstants skyPushConstants{
        .inverseViewProjection = transform.inverseViewProjection,
        .cameraPosition = glm::vec4(cameraPosition, 0.0f),
    };
    skyPipeline_->bind(commandBuffer);
    textureDescriptors_.bind(commandBuffer, skyPipeline_->layout());
    vkCmdPushConstants(
        commandBuffer, skyPipeline_->layout(), VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(SkyPushConstants), &skyPushConstants);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);

    const VkBuffer vertexBuffers[] = {vertexBuffer_.handle()};
    const VkDeviceSize vertexOffsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, vertexOffsets);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer_.handle(), 0, VK_INDEX_TYPE_UINT32);
    duckPipeline_->bind(commandBuffer);
    textureDescriptors_.bind(commandBuffer, duckPipeline_->layout());

    struct PushConstants {
        glm::mat4 mvp;
        glm::vec4 centerRadius;
        glm::vec4 cameraAndAngle;
        uint32_t texture2DId = 0;
        uint32_t cubeTextureId = 0;
    };
    const PushConstants pushConstants{
        .mvp = transform.mvp,
        .centerRadius = {meshCenter_[0], meshCenter_[1], meshCenter_[2], meshRadius_},
        .cameraAndAngle = glm::vec4(cameraPosition, transform.rotationAngle),
        .texture2DId = 0,
        .cubeTextureId = 0,
    };
    vkCmdPushConstants(
        commandBuffer, duckPipeline_->layout(),
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(PushConstants), &pushConstants);
    vkCmdDrawIndexed(commandBuffer, indexCount_, 1, 0, 0, 0);

    // 线条画布复用当前颜色与深度附件，因此能正确被模型遮挡。
    buildDebugCanvas();
    lineCanvas_.record(commandBuffer, imageIndex, frame.view);
    vkCmdEndRendering(commandBuffer);

    vulkan_utils::transitionImage(
        commandBuffer, image, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    imageLayouts_[imageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    depthLayout_ = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
}

void ReflectiveDuckCommandRecorder::createDepthAttachment() {
    const VkExtent2D extent = swapchain_.extent();
    const VkImageCreateInfo imageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = DepthFormat,
        .extent = {extent.width, extent.height, 1},
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
            context_.allocator(), &imageCreateInfo, &allocationCreateInfo,
            &depthImage_, &depthAllocation_, nullptr),
        "vmaCreateImage");
    context_.setDebugObjectName(
        VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(depthImage_),
        "Reflective duck depth image");

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
        VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(depthImageView_),
        "Reflective duck depth image view");
}

void ReflectiveDuckCommandRecorder::destroyDepthAttachment() {
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
