#pragma once

#include <vulkan/vulkan.h>

#if APP_ENABLE_TRACY
#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>

#define APP_PROFILE_FUNCTION() ZoneScoped
#define APP_PROFILE_SCOPE(name) ZoneScopedN(name)
#define APP_PROFILE_FRAME() FrameMark
#define APP_PROFILE_THREAD(name) tracy::SetThreadName(name)
#define APP_PROFILE_GPU_ZONE(context, commandBuffer, name) \
    TracyVkNamedZone((context).profiler().gpuContext(), __appTracyGpuZone, \
        commandBuffer, name, (context).profiler().gpuEnabled())
#else
#define APP_PROFILE_FUNCTION()
#define APP_PROFILE_SCOPE(name)
#define APP_PROFILE_FRAME()
#define APP_PROFILE_THREAD(name)
#define APP_PROFILE_GPU_ZONE(context, commandBuffer, name)
#endif

class VulkanContext;

// Tracy 的 Vulkan query pool 只在这里创建和销毁；调用方只负责标记关心的范围。
class VulkanProfiler {
public:
    explicit VulkanProfiler(const VulkanContext& context);
    ~VulkanProfiler();

    VulkanProfiler(const VulkanProfiler&) = delete;
    VulkanProfiler& operator=(const VulkanProfiler&) = delete;

    bool gpuEnabled() const;
    void collect(VkCommandBuffer commandBuffer) const;

#if APP_ENABLE_TRACY
    TracyVkCtx gpuContext() const;
#endif

private:
    const VulkanContext& context_;
#if APP_ENABLE_TRACY
    TracyVkCtx gpuContext_ = nullptr;
#endif
};
