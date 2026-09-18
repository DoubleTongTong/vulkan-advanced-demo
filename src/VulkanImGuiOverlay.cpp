#include "VulkanImGuiOverlay.h"

#include "Image.h"
#include "VulkanBindlessDescriptorSet.h"
#include "VulkanContext.h"
#include "VulkanProfiler.h"
#include "VulkanRenderPipeline.h"
#include "VulkanShaderModule.h"
#include "VulkanSwapchain.h"
#include "VulkanTexture2D.h"
#include "VulkanUtils.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace {

constexpr uint32_t FontTextureId = 1;
constexpr uint32_t PreviewTextureId = 2;

struct PushConstants {
    float scale[2];
    float translate[2];
    uint32_t textureId;
};

} // namespace

VulkanImGuiOverlay::VulkanImGuiOverlay(
    const VulkanContext& context,
    const VulkanSwapchain& swapchain,
    GLFWwindow* window,
    const VulkanTexture2D& previewTexture)
    : context_(context), swapchain_(swapchain) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    bool glfwInitialized = false;
    try {
        glfwInitialized = ImGui_ImplGlfw_InitForVulkan(window, true);
        if (!glfwInitialized) {
            throw std::runtime_error("Failed to initialize ImGui GLFW input.");
        }
        ImGui::GetIO().BackendRendererName = "imgui-vulkan-advanced-demo";
        ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::StyleColorsDark();

        createFontTexture();
        descriptors_ = std::make_unique<VulkanBindlessDescriptorSet>(context_, 8, 1, "ImGui textures");
        descriptors_->fillSamplers(fontTexture_->sampler());
        descriptors_->writeTexture2D(FontTextureId, *fontTexture_);
        descriptors_->writeTexture2D(PreviewTextureId, previewTexture);
        createPipeline();
    } catch (...) {
        if (glfwInitialized) {
            ImGui_ImplGlfw_Shutdown();
        }
        ImGui::DestroyContext();
        throw;
    }
}

VulkanImGuiOverlay::~VulkanImGuiOverlay() {
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void VulkanImGuiOverlay::createFontTexture() {
    // ImGui 生成 RGBA 字体图集；VulkanTexture2D 负责上传到 GPU。
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    RgbaImage image{
        .width = static_cast<uint32_t>(width),
        .height = static_cast<uint32_t>(height),
        .pixels = std::vector<uint8_t>(pixels, pixels + width * height * 4),
    };
    fontTexture_ = std::make_unique<VulkanTexture2D>(context_, image, "ImGui font atlas");
    ImGui::GetIO().Fonts->SetTexID(static_cast<ImTextureID>(FontTextureId));
    ImGui::GetIO().Fonts->ClearTexData();
}

void VulkanImGuiOverlay::createPipeline() {
    const std::filesystem::path shaderDir = APP_IMGUI_SHADER_DIR;
    const VulkanShaderModule vertex = VulkanShaderModule::fromFile(context_, shaderDir / "main.vert");
    const VulkanShaderModule fragment = VulkanShaderModule::fromFile(context_, shaderDir / "main.frag");
    pipeline_ = std::make_unique<VulkanRenderPipeline>(context_, RenderPipelineDesc{
        .vertexShader = &vertex,
        .fragmentShader = &fragment,
        .vertexBindings = {{0, sizeof(ImDrawVert), VK_VERTEX_INPUT_RATE_VERTEX}},
        .vertexAttributes = {
            {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, pos)},
            {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, uv)},
            {2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(ImDrawVert, col)},
        },
        .colorFormat = swapchain_.imageFormat(),
        .blendEnabled = true,
        .descriptorSetLayouts = {descriptors_->layout()},
        .debugName = "ImGui overlay pipeline",
    });
}

void VulkanImGuiOverlay::uploadDrawData(uint32_t frameIndex) {
    APP_PROFILE_FUNCTION();
    const ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData->TotalVtxCount == 0) {
        return;
    }

    FrameBuffers& buffers = frameBuffers_.at(frameIndex);
    // 主循环已等待同一 frame slot 的上次提交，这里扩容或覆盖缓冲都不会碰到 GPU 正在读取的数据。
    const size_t vertexBytes = static_cast<size_t>(drawData->TotalVtxCount) * sizeof(ImDrawVert);
    const size_t indexBytes = static_cast<size_t>(drawData->TotalIdxCount) * sizeof(ImDrawIdx);
    if (!buffers.vertices || buffers.vertices->size() < vertexBytes) {
        const VkDeviceSize capacity = std::max<VkDeviceSize>(vertexBytes, buffers.vertices ? buffers.vertices->size() * 2 : 65536);
        buffers.vertices = std::make_unique<VulkanBuffer>(context_, BufferDesc{
            .usage = BufferUsage_Vertex,
            .storage = BufferStorage::HostVisible,
            .size = capacity,
            .debugName = "ImGui vertices",
        });
    }
    if (!buffers.indices || buffers.indices->size() < indexBytes) {
        const VkDeviceSize capacity = std::max<VkDeviceSize>(indexBytes, buffers.indices ? buffers.indices->size() * 2 : 16384);
        buffers.indices = std::make_unique<VulkanBuffer>(context_, BufferDesc{
            .usage = BufferUsage_Index,
            .storage = BufferStorage::HostVisible,
            .size = capacity,
            .debugName = "ImGui indices",
        });
    }

    std::vector<ImDrawVert> vertices;
    std::vector<ImDrawIdx> indices;
    vertices.reserve(drawData->TotalVtxCount);
    indices.reserve(drawData->TotalIdxCount);
    // 一个 ImGui 帧可能有多个 draw list，先拼成连续数组，再分别上传一次。
    for (int i = 0; i < drawData->CmdListsCount; ++i) {
        const ImDrawList& list = *drawData->CmdLists[i];
        vertices.insert(vertices.end(), list.VtxBuffer.begin(), list.VtxBuffer.end());
        indices.insert(indices.end(), list.IdxBuffer.begin(), list.IdxBuffer.end());
    }
    buffers.vertices->bufferSubData(0, vertexBytes, vertices.data());
    buffers.indices->bufferSubData(0, indexBytes, indices.data());
}

void VulkanImGuiOverlay::bindDrawState(
    VkCommandBuffer commandBuffer, uint32_t frameIndex, float width, float height) const {
    const VkViewport viewport{0.0f, 0.0f, width, height, 0.0f, 1.0f};
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    pipeline_->bind(commandBuffer);
    descriptors_->bind(commandBuffer, pipeline_->layout());

    const FrameBuffers& buffers = frameBuffers_.at(frameIndex);
    const VkBuffer vertexBuffers[] = {buffers.vertices->handle()};
    const VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, buffers.indices->handle(), 0,
        sizeof(ImDrawIdx) == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
}

void VulkanImGuiOverlay::record(VkCommandBuffer commandBuffer, uint32_t imageIndex, uint32_t frameIndex) {
    APP_PROFILE_FUNCTION();
    // 输入已在主循环中轮询；这里统一完成 ImGui 界面和 Vulkan 绘制命令。
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGui::Begin("Texture Viewer");
    ImGui::Image(static_cast<ImTextureID>(PreviewTextureId), ImVec2(320, 320));
    ImGui::End();
    ImGui::ShowDemoWindow();
    ImGui::Render();
    const ImDrawData* drawData = ImGui::GetDrawData();
    const float width = drawData->DisplaySize.x * drawData->FramebufferScale.x;
    const float height = drawData->DisplaySize.y * drawData->FramebufferScale.y;
    if (drawData->TotalVtxCount == 0 || width <= 0.0f || height <= 0.0f) {
        return;
    }
    uploadDrawData(frameIndex);
    APP_PROFILE_GPU_ZONE(context_, commandBuffer, "ImGui overlay");

    const VkImage image = swapchain_.images().at(imageIndex);
    // 场景已经画好且图像处于 PRESENT；切回颜色附件，并用 LOAD 保留原画面。
    vulkan_utils::transitionImage(commandBuffer, image, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    const VkRenderingAttachmentInfo attachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = swapchain_.imageViews().at(imageIndex),
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };
    const VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, swapchain_.extent()},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachment,
    };
    vkCmdBeginRendering(commandBuffer, &renderingInfo);
    bindDrawState(commandBuffer, frameIndex, width, height);

    const ImVec2 clipOffset = drawData->DisplayPos;
    const ImVec2 clipScale = drawData->FramebufferScale;
    // ImGui 给出逻辑坐标；push constants 将顶点映射到 Vulkan 裁剪空间。
    const float sx = 2.0f / drawData->DisplaySize.x;
    const float sy = 2.0f / drawData->DisplaySize.y;
    uint32_t indexOffset = 0;
    int32_t vertexOffset = 0;
    for (int i = 0; i < drawData->CmdListsCount; ++i) {
        const ImDrawList& list = *drawData->CmdLists[i];
        for (const ImDrawCmd& draw : list.CmdBuffer) {
            if (draw.UserCallback) {
                if (draw.UserCallback == ImDrawCallback_ResetRenderState) {
                    bindDrawState(commandBuffer, frameIndex, width, height);
                } else {
                    draw.UserCallback(&list, &draw);
                }
                continue;
            }

            // ClipRect 是逻辑坐标；scissor 需要 framebuffer 像素坐标。
            const float left = std::clamp((draw.ClipRect.x - clipOffset.x) * clipScale.x, 0.0f, width);
            const float top = std::clamp((draw.ClipRect.y - clipOffset.y) * clipScale.y, 0.0f, height);
            const float right = std::clamp((draw.ClipRect.z - clipOffset.x) * clipScale.x, 0.0f, width);
            const float bottom = std::clamp((draw.ClipRect.w - clipOffset.y) * clipScale.y, 0.0f, height);
            if (right <= left || bottom <= top) {
                continue;
            }
            if (draw.TextureId >= descriptors_->maxTextures()) {
                throw std::runtime_error("ImGui texture ID is outside the bindless table.");
            }
            const VkRect2D scissor{
                .offset = {static_cast<int32_t>(left), static_cast<int32_t>(top)},
                .extent = {
                    static_cast<uint32_t>(right) - static_cast<uint32_t>(left),
                    static_cast<uint32_t>(bottom) - static_cast<uint32_t>(top),
                },
            };
            vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

            const PushConstants push{
                .scale = {sx, sy},
                .translate = {-1.0f - clipOffset.x * sx, -1.0f - clipOffset.y * sy},
                .textureId = static_cast<uint32_t>(draw.TextureId),
            };
            vkCmdPushConstants(commandBuffer, pipeline_->layout(),
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
            vkCmdDrawIndexed(commandBuffer, draw.ElemCount, 1,
                indexOffset + draw.IdxOffset, vertexOffset + static_cast<int32_t>(draw.VtxOffset), 0);
        }
        // 下一张 draw list 在合并后的大缓冲中，从这里继续计数。
        indexOffset += static_cast<uint32_t>(list.IdxBuffer.Size);
        vertexOffset += list.VtxBuffer.Size;
    }
    vkCmdEndRendering(commandBuffer);

    vulkan_utils::transitionImage(commandBuffer, image, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
}
