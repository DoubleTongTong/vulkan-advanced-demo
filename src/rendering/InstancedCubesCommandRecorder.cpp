#include "rendering/InstancedCubesCommandRecorder.h"

#include "Image.h"
#include "VulkanContext.h"
#include "VulkanProfiler.h"
#include "VulkanShaderModule.h"
#include "VulkanSwapchain.h"
#include "VulkanUtils.h"

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <stdexcept>

namespace {

constexpr VkFormat DepthFormat = VK_FORMAT_D32_SFLOAT;
constexpr uint32_t CubeCount = 1024 * 1024;
constexpr uint32_t VertexCount = 36;
constexpr uint32_t MaxBindlessTextures = 16;
constexpr const char* SamplersResource = "kSamplers";
constexpr const char* TexturesResource = "kTextures2D";

// 固定种子的 xorshift 足够生成演示数据，也让每次启动看到相同的场景。
float random01(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(state) / static_cast<float>(UINT32_MAX);
}

std::vector<glm::vec4> makeInstances() {
    std::vector<glm::vec4> instances(CubeCount);
    uint32_t randomState = 0x12345678u;
    for (glm::vec4& instance : instances) {
        // xyz 是中心位置，w 是初始旋转角度；一个 vec4 恰好 16 字节。
        instance = {
            random01(randomState) * 1000.0f - 500.0f,
            random01(randomState) * 1000.0f - 500.0f,
            random01(randomState) * 1000.0f - 500.0f,
            random01(randomState) * 6.2831853f,
        };
    }
    return instances;
}

VulkanBuffer makeInstanceBuffer(const VulkanContext& context) {
    // 数据会在 VulkanBuffer 构造期间立即上传，返回后即可释放这份 CPU 数组。
    const std::vector<glm::vec4> instances = makeInstances();
    return VulkanBuffer(
        context,
        {
            .usage = BufferUsage_Storage,
            .storage = BufferStorage::Device,
            .size = sizeof(glm::vec4) * instances.size(),
            .data = instances.data(),
            .debugName = "Instanced cubes positions and angles",
        });
}

RgbaImage makeXorTexture() {
    constexpr uint32_t Size = 256;
    RgbaImage image{.width = Size, .height = Size};
    image.pixels.resize(Size * Size * 4);
    for (uint32_t y = 0; y < Size; ++y) {
        for (uint32_t x = 0; x < Size; ++x) {
            const uint8_t value = static_cast<uint8_t>(x ^ y);
            const size_t offset = (y * Size + x) * 4;
            image.pixels[offset + 0] = value;
            image.pixels[offset + 1] = value;
            image.pixels[offset + 2] = value;
            image.pixels[offset + 3] = 255;
        }
    }
    return image;
}

} // namespace

InstancedCubesCommandRecorder::InstancedCubesCommandRecorder(
    const VulkanContext& context,
    const VulkanSwapchain& swapchain,
    const VulkanShaderModule& vertexShader,
    const VulkanShaderModule& fragmentShader)
    : context_(context),
      swapchain_(swapchain),
      texture_(context, makeXorTexture(), "Instanced cubes XOR texture"),
      instanceBuffer_(makeInstanceBuffer(context)),
      instanceDescriptors_(
          context,
          {
              .shaders = {&vertexShader},
              .set = 1,
              .debugName = "Instanced cubes instance descriptors",
          }),
      textureDescriptors_(
          context,
          {
              .shaders = {&fragmentShader},
              .runtimeArrays = {{TexturesResource, MaxBindlessTextures}},
              .debugName = "Instanced cubes texture descriptors",
          }),
      startTime_(std::chrono::steady_clock::now()),
      imageLayouts_(swapchain.images().size(), VK_IMAGE_LAYOUT_UNDEFINED) {
    if ((swapchain.imageUsage() & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
        throw std::runtime_error("Swapchain images do not support color attachment usage.");
    }

    instanceDescriptors_.writeStorageBuffer("kInstances", instanceBuffer_);
    textureDescriptors_.fillSamplers(SamplersResource, texture_.sampler());
    textureDescriptors_.writeTexture2D(TexturesResource, 0, texture_);
    createDepthAttachment();
    pipeline_ = std::make_unique<VulkanRenderPipeline>(
        context_,
        RenderPipelineDesc{
            .vertexShader = &vertexShader,
            .fragmentShader = &fragmentShader,
            // 不需要 vertex buffer：36 个顶点完全由 gl_VertexIndex 生成。
            .colorFormat = swapchain_.imageFormat(),
            .depthFormat = DepthFormat,
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .depthTestEnabled = true,
            .depthWriteEnabled = true,
            .descriptorSetLayouts = {
                textureDescriptors_.layout(),
                instanceDescriptors_.layout(),
            },
            .debugName = "Instanced cubes pipeline",
        });
}

InstancedCubesCommandRecorder::~InstancedCubesCommandRecorder() {
    pipeline_.reset();
    destroyDepthAttachment();
}

void InstancedCubesCommandRecorder::record(const RenderFrameContext& frame) {
    const VkCommandBuffer commandBuffer = frame.commandBuffer;
    const uint32_t imageIndex = frame.imageIndex;
    APP_PROFILE_FUNCTION();
    APP_PROFILE_GPU_ZONE(context_, commandBuffer, "One million instanced cubes");

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

    // 白色背景更容易看清近处立方体的轮廓。
    const VkClearValue clearColor{.color = {{1.0f, 1.0f, 1.0f, 1.0f}}};
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

    pipeline_->bind(commandBuffer);
    textureDescriptors_.bind(commandBuffer, pipeline_->layout());
    instanceDescriptors_.bind(commandBuffer, pipeline_->layout());

    struct PushConstants {
        glm::mat4 viewProjection;
        float time;
    };
    const float seconds = std::chrono::duration<float>(
        std::chrono::steady_clock::now() - startTime_).count();
    const PushConstants pushConstants{
        .viewProjection = frame.view.viewProjection,
        .time = seconds,
    };
    vkCmdPushConstants(
        commandBuffer, pipeline_->layout(), VK_SHADER_STAGE_VERTEX_BIT,
        0, sizeof(PushConstants), &pushConstants);

    // 这是整个示例的重点：一个 API 调用提交一百万个实例。
    vkCmdDraw(commandBuffer, VertexCount, CubeCount, 0, 0);
    vkCmdEndRendering(commandBuffer);

    vulkan_utils::transitionImage(
        commandBuffer, image, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    imageLayouts_[imageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    depthLayout_ = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
}

void InstancedCubesCommandRecorder::createDepthAttachment() {
    const VkExtent2D extent = swapchain_.extent();
    const VkImageCreateInfo imageInfo{
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
    const VmaAllocationCreateInfo allocationInfo{
        .usage = VMA_MEMORY_USAGE_AUTO,
        .preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };
    vulkan_utils::checkVk(
        vmaCreateImage(
            context_.allocator(), &imageInfo, &allocationInfo,
            &depthImage_, &depthAllocation_, nullptr),
        "vmaCreateImage");
    context_.setDebugObjectName(
        VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(depthImage_),
        "Instanced cubes depth image");

    const VkImageViewCreateInfo viewInfo{
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
        vkCreateImageView(context_.device(), &viewInfo, nullptr, &depthImageView_),
        "vkCreateImageView");
    context_.setDebugObjectName(
        VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(depthImageView_),
        "Instanced cubes depth image view");
}

void InstancedCubesCommandRecorder::destroyDepthAttachment() {
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
