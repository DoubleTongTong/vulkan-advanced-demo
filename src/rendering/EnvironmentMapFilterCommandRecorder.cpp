#include "rendering/EnvironmentMapFilterCommandRecorder.h"

#include "CubeMapProcessor.h"
#include "Image.h"
#include "ImageProcessor.h"
#include "VulkanContext.h"
#include "VulkanShaderModule.h"
#include "VulkanTextureCube.h"

#include <ktx.h>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr const char* EnvironmentSamplerResource = "kEnvironmentSampler";
constexpr const char* EnvironmentTextureResource = "kEnvironmentCube";
constexpr const char* FilteredOutputResource = "kFilteredOutput";
constexpr VkDeviceSize BytesPerTexel = 4 * sizeof(float);

uint32_t maximumMipLevels(uint32_t faceSize) {
    uint32_t levels = 0;
    for (uint32_t size = faceSize; size > 0; size /= 2) {
        ++levels;
    }
    return levels;
}

VkDeviceSize checkedOutputByteSize(const EnvironmentMapFilterConfig& config) {
    if (config.faceSize == 0 || config.mipLevels == 0 || config.sampleCount == 0) {
        throw std::invalid_argument("Environment filter dimensions and sample count must be non-zero.");
    }
    if (config.mipLevels > maximumMipLevels(config.faceSize)) {
        throw std::invalid_argument("Environment filter mip count exceeds the face size.");
    }
    if (config.distribution > EnvironmentMapDistribution::Charlie) {
        throw std::invalid_argument("Unknown environment filter distribution.");
    }

    VkDeviceSize texelCount = 0;
    uint32_t mipSize = config.faceSize;
    for (uint32_t mip = 0; mip < config.mipLevels; ++mip) {
        const VkDeviceSize faceTexels = static_cast<VkDeviceSize>(mipSize) * mipSize;
        if (faceTexels > (std::numeric_limits<VkDeviceSize>::max() - texelCount) / 6) {
            throw std::overflow_error("Environment filter texel count overflow.");
        }
        texelCount += faceTexels * 6;
        mipSize = std::max(mipSize / 2, 1u);
    }
    if (texelCount > std::numeric_limits<VkDeviceSize>::max() / BytesPerTexel) {
        throw std::overflow_error("Environment filter buffer size overflow.");
    }
    if (texelCount > std::numeric_limits<uint32_t>::max()) {
        throw std::overflow_error("Environment filter texel offsets exceed uint32_t range.");
    }
    return texelCount * BytesPerTexel;
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

struct FilterPushConstants {
    uint32_t face = 0;
    float roughness = 0.0f;
    uint32_t sampleCount = 0;
    uint32_t targetFaceSize = 0;
    uint32_t sourceFaceSize = 0;
    uint32_t sourceMipLevels = 0;
    uint32_t distribution = 0;
    uint32_t outputTexelOffset = 0;
};

static_assert(sizeof(FilterPushConstants) == 32);

} // namespace

EnvironmentMapFilterCommandRecorder::EnvironmentMapFilterCommandRecorder(
    const VulkanContext& context,
    const VulkanShaderModule& computeShader,
    const VulkanTextureCube& environment,
    EnvironmentMapFilterConfig config)
    : config_(config),
      environment_(environment),
      outputBuffer_(
          context,
          BufferDesc{
              .usage = BufferUsage_Storage,
              .storage = BufferStorage::HostReadback,
              .size = checkedOutputByteSize(config),
              .debugName = "Filtered environment readback buffer",
          }),
      descriptors_(
          context,
          DescriptorSetDesc{
              .shaders = {&computeShader},
              .debugName = "Environment filter descriptors",
          }),
      pipeline_(
          context,
          ComputePipelineDesc{
              .shader = &computeShader,
              .descriptorSetLayouts = {descriptors_.layout()},
              .debugName = "Environment convolution pipeline",
          }) {
    descriptors_.writeSampler(EnvironmentSamplerResource, 0, environment_.sampler());
    descriptors_.writeTextureCube(EnvironmentTextureResource, 0, environment_);
    descriptors_.writeStorageBuffer(FilteredOutputResource, outputBuffer_);
}

void EnvironmentMapFilterCommandRecorder::record(VkCommandBuffer commandBuffer) const {
    if (!commandBuffer) {
        throw std::invalid_argument("Environment filter command buffer cannot be null.");
    }

    pipeline_.bind(commandBuffer);
    descriptors_.bindCompute(commandBuffer, pipeline_.layout());

    uint32_t mipSize = config_.faceSize;
    uint32_t outputTexelOffset = 0;
    for (uint32_t mip = 0; mip < config_.mipLevels; ++mip) {
        const float roughness = config_.mipLevels > 1
            ? static_cast<float>(mip) / static_cast<float>(config_.mipLevels - 1)
            : 0.0f;

        for (uint32_t face = 0; face < 6; ++face) {
            const FilterPushConstants pushConstants{
                .face = face,
                .roughness = roughness,
                .sampleCount = config_.sampleCount,
                .targetFaceSize = mipSize,
                .sourceFaceSize = environment_.faceSize(),
                .sourceMipLevels = environment_.mipLevels(),
                .distribution = static_cast<uint32_t>(config_.distribution),
                .outputTexelOffset = outputTexelOffset,
            };
            vkCmdPushConstants(
                commandBuffer,
                pipeline_.layout(),
                VK_SHADER_STAGE_COMPUTE_BIT,
                0,
                sizeof(pushConstants),
                &pushConstants);

            const uint32_t groups = (mipSize + LocalSize - 1) / LocalSize;
            vkCmdDispatch(commandBuffer, groups, groups, 1);
            outputTexelOffset += mipSize * mipSize;
        }
        mipSize = std::max(mipSize / 2, 1u);
    }
}

void EnvironmentMapFilterCommandRecorder::saveKtx2(
    const std::filesystem::path& outputPath) const {
    if (outputPath.empty()) {
        throw std::invalid_argument("Filtered environment output path cannot be empty.");
    }

    std::vector<uint8_t> pixels(static_cast<size_t>(outputBuffer_.size()));
    outputBuffer_.readSubData(0, pixels.size(), pixels.data());

    const ktxTextureCreateInfo createInfo{
        .vkFormat = VK_FORMAT_R32G32B32A32_SFLOAT,
        .baseWidth = config_.faceSize,
        .baseHeight = config_.faceSize,
        .baseDepth = 1,
        .numDimensions = 2,
        .numLevels = config_.mipLevels,
        .numLayers = 1,
        .numFaces = 6,
        .isArray = KTX_FALSE,
        .generateMipmaps = KTX_FALSE,
    };

    ktxTexture2* rawTexture = nullptr;
    checkKtx(
        ktxTexture2_Create(&createInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &rawTexture),
        "Failed to create filtered environment KTX2 texture");
    const std::unique_ptr<ktxTexture2, KtxTextureDeleter> texture(rawTexture);

    size_t byteOffset = 0;
    uint32_t mipSize = config_.faceSize;
    for (uint32_t mip = 0; mip < config_.mipLevels; ++mip) {
        const size_t faceByteSize =
            static_cast<size_t>(mipSize) * mipSize * static_cast<size_t>(BytesPerTexel);
        for (uint32_t face = 0; face < 6; ++face) {
            checkKtx(
                ktxTexture_SetImageFromMemory(
                    ktxTexture(texture.get()),
                    mip,
                    0,
                    face,
                    pixels.data() + byteOffset,
                    faceByteSize),
                "Failed to copy filtered cube face into KTX2 texture");
            byteOffset += faceByteSize;
        }
        mipSize = std::max(mipSize / 2, 1u);
    }
    if (byteOffset != pixels.size()) {
        throw std::runtime_error("Filtered environment KTX2 byte count mismatch.");
    }

    const std::filesystem::path parent = outputPath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    checkKtx(
        ktxTexture_WriteToNamedFile(ktxTexture(texture.get()), outputPath.string().c_str()),
        "Failed to write filtered environment KTX2 texture");
}

void EnvironmentMapFilterCommandRecorder::saveEquirectangularHdrMipChain(
    const std::filesystem::path& outputDirectory,
    std::string_view baseName) const {
    if (outputDirectory.empty() || baseName.empty()) {
        throw std::invalid_argument("HDR output directory and base name cannot be empty.");
    }

    std::vector<float> pixels(
        static_cast<size_t>(outputBuffer_.size() / sizeof(float)));
    outputBuffer_.readSubData(
        0,
        static_cast<size_t>(outputBuffer_.size()),
        pixels.data());

    const CubeMapProcessor cubeMapProcessor;
    const ImageProcessor imageProcessor;
    size_t floatOffset = 0;
    uint32_t mipSize = config_.faceSize;
    for (uint32_t mip = 0; mip < config_.mipLevels; ++mip) {
        const size_t mipFloatCount =
            static_cast<size_t>(mipSize) * mipSize * 6u * 4u;
        CubeMapImage cubeMap;
        cubeMap.faceSize = mipSize;
        cubeMap.pixels.assign(
            pixels.begin() + static_cast<std::ptrdiff_t>(floatOffset),
            pixels.begin() + static_cast<std::ptrdiff_t>(floatOffset + mipFloatCount));

        const RgbaFloatImage equirectangular =
            cubeMapProcessor.toEquirectangular(cubeMap);
        const std::filesystem::path outputPath =
            outputDirectory /
            (std::string(baseName) + "_mip_" + std::to_string(mip) + ".hdr");
        imageProcessor.saveHdr(outputPath, equirectangular);

        floatOffset += mipFloatCount;
        mipSize = std::max(mipSize / 2, 1u);
    }
    if (floatOffset != pixels.size()) {
        throw std::runtime_error("Filtered environment HDR byte count mismatch.");
    }
}

const EnvironmentMapFilterConfig& EnvironmentMapFilterCommandRecorder::config() const {
    return config_;
}
