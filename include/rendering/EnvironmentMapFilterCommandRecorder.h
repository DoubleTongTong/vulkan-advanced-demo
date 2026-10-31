#pragma once

#include "VulkanBuffer.h"
#include "VulkanComputePipeline.h"
#include "VulkanDescriptorSet.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <string_view>

class VulkanContext;
class VulkanShaderModule;
class VulkanTextureCube;

enum class EnvironmentMapDistribution : uint32_t {
    Lambertian = 0,
    Ggx = 1,
    Charlie = 2,
};

struct EnvironmentMapFilterConfig {
    EnvironmentMapDistribution distribution = EnvironmentMapDistribution::Lambertian;
    uint32_t faceSize = 64;
    uint32_t mipLevels = 1;
    uint32_t sampleCount = 512;
};

// 把 HDR 环境立方体离线卷积为 Lambertian、GGX 或 Charlie KTX2 资源。
// 一个对象对应一种分布，便于明确管理输出尺寸、mip 数量和回读缓冲区。
class EnvironmentMapFilterCommandRecorder final {
public:
    EnvironmentMapFilterCommandRecorder(
        const VulkanContext& context,
        const VulkanShaderModule& computeShader,
        const VulkanTextureCube& environment,
        EnvironmentMapFilterConfig config);

    void record(VkCommandBuffer commandBuffer) const;
    void saveKtx2(const std::filesystem::path& outputPath) const;
    void saveEquirectangularHdrMipChain(
        const std::filesystem::path& outputDirectory,
        std::string_view baseName) const;

    const EnvironmentMapFilterConfig& config() const;

private:
    static constexpr uint32_t LocalSize = 8;

    EnvironmentMapFilterConfig config_;
    const VulkanTextureCube& environment_;
    VulkanBuffer outputBuffer_;
    VulkanDescriptorSet descriptors_;
    VulkanComputePipeline pipeline_;
};
