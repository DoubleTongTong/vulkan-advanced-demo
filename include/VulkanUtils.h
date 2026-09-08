#pragma once

#include <vulkan/vulkan.h>

namespace vulkan_utils {

// Vulkan 大部分 API 都返回 VkResult，集中检查能让后续代码少一点重复噪音。
void checkVk(VkResult result, const char* action);

} // namespace vulkan_utils
