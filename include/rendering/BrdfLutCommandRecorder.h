#pragma once

#include "VulkanBuffer.h"
#include "VulkanComputePipeline.h"
#include "VulkanDescriptorSet.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>

class VulkanContext;
class VulkanShaderModule;

struct BrdfLutConfig {
    uint32_t width = 256;
    uint32_t height = 256;
    uint32_t sampleCount = 1024;
};

// 记录一次离线 BRDF 积分计算，并在 GPU 完成后把 RGBA16F 结果保存成 KTX2。
class BrdfLutCommandRecorder final {
public:
    BrdfLutCommandRecorder(
        const VulkanContext& context,
        const VulkanShaderModule& computeShader,
        BrdfLutConfig config = {});

    void record(VkCommandBuffer commandBuffer) const;
    void saveKtx2(const std::filesystem::path& outputPath) const;

    const BrdfLutConfig& config() const;

private:
    static constexpr uint32_t LocalSize = 16;

    BrdfLutConfig config_;
    VulkanBuffer outputBuffer_;
    VulkanDescriptorSet descriptors_;

    // sampleCount 通过 specialization constant 固化到具体 pipeline。
    VkSpecializationMapEntry sampleCountEntry_{};
    VkSpecializationInfo specializationInfo_{};
    VulkanComputePipeline pipeline_;
};
