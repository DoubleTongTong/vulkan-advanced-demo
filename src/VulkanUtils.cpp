#include "VulkanUtils.h"

#include <string>
#include <stdexcept>

namespace vulkan_utils {

void checkVk(VkResult result, const char* action) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(action) + " failed with VkResult " + std::to_string(result) + ".");
    }
}

} // namespace vulkan_utils
