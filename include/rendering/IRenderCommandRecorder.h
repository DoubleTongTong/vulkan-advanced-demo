#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

class IRenderCommandRecorder {
public:
    virtual ~IRenderCommandRecorder() = default;

    virtual void record(VkCommandBuffer commandBuffer, uint32_t imageIndex) = 0;
};
