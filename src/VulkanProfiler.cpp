#include "VulkanProfiler.h"

#include "VulkanContext.h"
#include "VulkanUtils.h"

#include <iostream>
#include <vector>

VulkanProfiler::VulkanProfiler(const VulkanContext& context) : context_(context) {
#if APP_ENABLE_TRACY
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(context.physicalDevice(), &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(context.physicalDevice(), &count, families.data());
    if (families.at(context.graphicsQueueFamilyIndex()).timestampValidBits == 0) {
        std::cout << "Tracy GPU profiling unavailable: graphics queue has no timestamps.\n";
        return;
    }

    const VkCommandPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = context.graphicsQueueFamilyIndex(),
    };
    VkCommandPool pool = VK_NULL_HANDLE;
    vulkan_utils::checkVk(vkCreateCommandPool(context.device(), &poolInfo, nullptr, &pool), "vkCreateCommandPool (Tracy)");
    try {
        const VkCommandBufferAllocateInfo bufferInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        VkCommandBuffer buffer = VK_NULL_HANDLE;
        vulkan_utils::checkVk(vkAllocateCommandBuffers(context.device(), &bufferInfo, &buffer),
            "vkAllocateCommandBuffers (Tracy)");

        // Tracy 0.11.1 只在创建 GPU context 时用这块临时 command buffer。
        const auto getDomains = reinterpret_cast<PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT>(
            vkGetInstanceProcAddr(context.instance(), "vkGetPhysicalDeviceCalibrateableTimeDomainsEXT"));
        const auto getTimestamps = reinterpret_cast<PFN_vkGetCalibratedTimestampsEXT>(
            vkGetDeviceProcAddr(context.device(), "vkGetCalibratedTimestampsEXT"));
        if (context.calibratedTimestampsEnabled() && getDomains && getTimestamps) {
            gpuContext_ = TracyVkContextCalibrated(context.physicalDevice(), context.device(),
                context.graphicsQueue(), buffer, getDomains, getTimestamps);
        } else {
            gpuContext_ = TracyVkContext(context.physicalDevice(), context.device(), context.graphicsQueue(), buffer);
        }

        constexpr char Name[] = "Graphics queue";
        TracyVkContextName(gpuContext_, Name, sizeof(Name) - 1);
    } catch (...) {
        vkDestroyCommandPool(context.device(), pool, nullptr);
        throw;
    }
    vkDestroyCommandPool(context.device(), pool, nullptr);
#endif
}

VulkanProfiler::~VulkanProfiler() {
#if APP_ENABLE_TRACY
    if (gpuContext_) {
        vkDeviceWaitIdle(context_.device());
        TracyVkDestroy(gpuContext_);
    }
#endif
}

bool VulkanProfiler::gpuEnabled() const {
#if APP_ENABLE_TRACY
    return gpuContext_ != nullptr;
#else
    return false;
#endif
}

void VulkanProfiler::collect(VkCommandBuffer commandBuffer) const {
#if APP_ENABLE_TRACY
    if (gpuContext_) {
        TracyVkCollect(gpuContext_, commandBuffer);
    }
#else
    (void)commandBuffer;
#endif
}

#if APP_ENABLE_TRACY
TracyVkCtx VulkanProfiler::gpuContext() const {
    return gpuContext_;
}
#endif
