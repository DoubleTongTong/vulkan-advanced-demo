#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>
#include <vector>

class VulkanContext;
class VulkanShaderModule;

struct RenderPipelineDesc {
    const VulkanShaderModule* vertexShader = nullptr;
    const VulkanShaderModule* fragmentShader = nullptr;
    std::vector<VkVertexInputBindingDescription> vertexBindings;
    std::vector<VkVertexInputAttributeDescription> vertexAttributes;
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkCullModeFlags cullMode = VK_CULL_MODE_NONE;
    VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
    bool depthTestEnabled = false;
    bool depthWriteEnabled = false;
    VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    bool depthBiasEnabled = false;
    float depthBiasConstantFactor = 0.0f;
    float depthBiasSlopeFactor = 0.0f;
    std::vector<VkSpecializationMapEntry> specializationEntries;
    std::vector<uint8_t> specializationData;
    const char* debugName = "Render pipeline";
};

class VulkanRenderPipeline {
public:
    VulkanRenderPipeline(const VulkanContext& context, const RenderPipelineDesc& desc);
    ~VulkanRenderPipeline();

    VulkanRenderPipeline(const VulkanRenderPipeline&) = delete;
    VulkanRenderPipeline& operator=(const VulkanRenderPipeline&) = delete;

    VulkanRenderPipeline(VulkanRenderPipeline&& other) noexcept;
    VulkanRenderPipeline& operator=(VulkanRenderPipeline&& other) noexcept;

    VkPipeline handle() const;
    VkPipelineLayout layout() const;
    uint32_t pushConstantSize() const;

    void bind(VkCommandBuffer commandBuffer) const;

private:
    void createPipelineLayout(const RenderPipelineDesc& desc);
    void createPipeline(const RenderPipelineDesc& desc);
    void destroy();

    const VulkanContext* context_ = nullptr;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    uint32_t pushConstantSize_ = 0;
};
