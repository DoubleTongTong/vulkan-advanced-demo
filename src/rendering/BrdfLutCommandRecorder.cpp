#include "rendering/BrdfLutCommandRecorder.h"

#include "VulkanContext.h"
#include "VulkanShaderModule.h"

#include <ktx.h>

#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr const char* BrdfLutResource = "kBrdfLut";
constexpr VkDeviceSize BytesPerBrdfTexel = 4 * sizeof(uint16_t);

VkDeviceSize checkedOutputByteSize(const BrdfLutConfig& config) {
    if (config.width == 0 || config.height == 0 || config.sampleCount == 0) {
        throw std::invalid_argument("BRDF LUT dimensions and sample count must be greater than zero.");
    }

    const VkDeviceSize texelCount = static_cast<VkDeviceSize>(config.width) * config.height;
    if (texelCount > std::numeric_limits<VkDeviceSize>::max() / BytesPerBrdfTexel) {
        throw std::overflow_error("BRDF LUT buffer size overflow.");
    }
    return texelCount * BytesPerBrdfTexel;
}

struct KtxTextureDeleter {
    void operator()(ktxTexture2* texture) const {
        ktxTexture2_Destroy(texture);
    }
};

void checkKtx(KTX_error_code result, const char* action) {
    if (result != KTX_SUCCESS) {
        throw std::runtime_error(std::string(action) + ": " + ktxErrorString(result));
    }
}

} // namespace

BrdfLutCommandRecorder::BrdfLutCommandRecorder(
    const VulkanContext& context,
    const VulkanShaderModule& computeShader,
    BrdfLutConfig config)
    : config_(config),
      outputBuffer_(
          context,
          BufferDesc{
              .usage = BufferUsage_Storage,
              .storage = BufferStorage::HostReadback,
              .size = checkedOutputByteSize(config),
              .debugName = "BRDF LUT readback buffer",
          }),
      descriptors_(
          context,
          DescriptorSetDesc{
              .shaders = {&computeShader},
              .debugName = "BRDF LUT descriptors",
          }),
      sampleCountEntry_{
          .constantID = 0,
          .offset = 0,
          .size = sizeof(config_.sampleCount),
      },
      specializationInfo_{
          .mapEntryCount = 1,
          .pMapEntries = &sampleCountEntry_,
          .dataSize = sizeof(config_.sampleCount),
          .pData = &config_.sampleCount,
      },
      pipeline_(
          context,
          ComputePipelineDesc{
              .shader = &computeShader,
              .descriptorSetLayouts = {descriptors_.layout()},
              .specializationInfo = &specializationInfo_,
              .debugName = "BRDF LUT integration pipeline",
          }) {
    descriptors_.writeStorageBuffer(BrdfLutResource, outputBuffer_);
}

void BrdfLutCommandRecorder::record(VkCommandBuffer commandBuffer) const {
    if (!commandBuffer) {
        throw std::invalid_argument("BRDF LUT command buffer cannot be null.");
    }

    pipeline_.bind(commandBuffer);
    descriptors_.bindCompute(commandBuffer, pipeline_.layout());
    const uint32_t dimensions[2] = {config_.width, config_.height};
    vkCmdPushConstants(
        commandBuffer,
        pipeline_.layout(),
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(dimensions),
        dimensions);

    const uint32_t groupCountX = (config_.width + LocalSize - 1) / LocalSize;
    const uint32_t groupCountY = (config_.height + LocalSize - 1) / LocalSize;
    vkCmdDispatch(commandBuffer, groupCountX, groupCountY, 1);
}

void BrdfLutCommandRecorder::saveKtx2(const std::filesystem::path& outputPath) const {
    if (outputPath.empty()) {
        throw std::invalid_argument("BRDF LUT output path cannot be empty.");
    }

    std::vector<uint8_t> pixels(static_cast<size_t>(outputBuffer_.size()));
    outputBuffer_.readSubData(0, pixels.size(), pixels.data());

    const ktxTextureCreateInfo createInfo{
        .vkFormat = VK_FORMAT_R16G16B16A16_SFLOAT,
        .baseWidth = config_.width,
        .baseHeight = config_.height,
        .baseDepth = 1,
        .numDimensions = 2,
        .numLevels = 1,
        .numLayers = 1,
        .numFaces = 1,
        .isArray = KTX_FALSE,
        .generateMipmaps = KTX_FALSE,
    };

    ktxTexture2* rawTexture = nullptr;
    checkKtx(
        ktxTexture2_Create(&createInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &rawTexture),
        "Failed to create BRDF LUT KTX2 texture");
    const std::unique_ptr<ktxTexture2, KtxTextureDeleter> texture(rawTexture);

    checkKtx(
        ktxTexture_SetImageFromMemory(
            ktxTexture(texture.get()), 0, 0, 0, pixels.data(), pixels.size()),
        "Failed to copy BRDF LUT pixels into KTX2 texture");

    const std::filesystem::path parent = outputPath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    checkKtx(
        ktxTexture_WriteToNamedFile(ktxTexture(texture.get()), outputPath.string().c_str()),
        "Failed to write BRDF LUT KTX2 texture");
}

const BrdfLutConfig& BrdfLutCommandRecorder::config() const {
    return config_;
}
