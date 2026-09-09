#include "VulkanContext.h"

#include "VulkanUtils.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr const char* ValidationLayer = "VK_LAYER_KHRONOS_validation";
constexpr const char* RequiredDeviceExtensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

std::string deviceTypeName(VkPhysicalDeviceType type) {
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return "Discrete GPU";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return "Integrated GPU";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return "Virtual GPU";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return "CPU";
    default:
        return "Other";
    }
}

} // namespace

VulkanContext::VulkanContext(GLFWwindow* window) {
    // Vulkan 的启动顺序很固定：先 instance，再创建窗口 surface，
    // 然后挂上调试回调，选择物理显卡，最后基于它创建逻辑设备和队列。
    createInstance();
    if (validationEnabled_) {
        debugMessenger_ = std::make_unique<VulkanDebugMessenger>(instance_);
    }
    createSurface(window);
    pickPhysicalDevice();
    createLogicalDevice();
}

VulkanContext::~VulkanContext() {
    // 销毁顺序要和创建顺序相反：device 依赖 instance/surface，必须先释放。
    if (device_) {
        vkDestroyDevice(device_, nullptr);
    }

    if (surface_) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    }

    debugMessenger_.reset();

    if (instance_) {
        vkDestroyInstance(instance_, nullptr);
    }
}

VkInstance VulkanContext::instance() const {
    return instance_;
}

VkSurfaceKHR VulkanContext::surface() const {
    return surface_;
}

VkPhysicalDevice VulkanContext::physicalDevice() const {
    return physicalDevice_;
}

VkDevice VulkanContext::device() const {
    return device_;
}

VkQueue VulkanContext::graphicsQueue() const {
    return graphicsQueue_;
}

uint32_t VulkanContext::graphicsQueueFamilyIndex() const {
    return graphicsQueueFamilyIndex_;
}

void VulkanContext::setDebugObjectName(VkObjectType type, uint64_t handle, const char* name) const {
    if (!setDebugUtilsObjectName_ || !name || !*name) {
        return;
    }

    const VkDebugUtilsObjectNameInfoEXT nameInfo{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType = type,
        .objectHandle = handle,
        .pObjectName = name,
    };

    vulkan_utils::checkVk(
        setDebugUtilsObjectName_(device_, &nameInfo),
        "vkSetDebugUtilsObjectNameEXT");
}

void VulkanContext::createInstance() {
    if (!glfwVulkanSupported()) {
        throw std::runtime_error("GLFW reports Vulkan is not supported on this system.");
    }

    validationEnabled_ =
        validationLayerAvailable() &&
        instanceExtensionAvailable(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    if (!validationEnabled_) {
        std::cout << "Vulkan debug utils are not fully available, continue without validation callback.\n";
    }

    const VkApplicationInfo appInfo{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Vulkan Advanced Demo",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "Vulkan Advanced Demo",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_3,
    };

    const std::vector<const char*> extensions = requiredInstanceExtensions();

    // GLFW 会告诉我们当前平台创建 window surface 所需的 instance extensions。
    // Windows/Linux/macOS 的平台差异都被 GLFW 收在这里了。
    const VkInstanceCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = validationEnabled_ ? 1u : 0u,
        .ppEnabledLayerNames = validationEnabled_ ? &ValidationLayer : nullptr,
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };

    vulkan_utils::checkVk(vkCreateInstance(&createInfo, nullptr, &instance_), "vkCreateInstance");
}

void VulkanContext::createSurface(GLFWwindow* window) {
    if (!window) {
        throw std::runtime_error("Cannot create Vulkan surface with a null GLFW window.");
    }

    vulkan_utils::checkVk(glfwCreateWindowSurface(instance_, window, nullptr, &surface_), "glfwCreateWindowSurface");
}

void VulkanContext::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vulkan_utils::checkVk(vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr), "vkEnumeratePhysicalDevices");

    if (deviceCount == 0) {
        throw std::runtime_error("No Vulkan physical device was found.");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vulkan_utils::checkVk(vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data()), "vkEnumeratePhysicalDevices");

    // 优先选择独显，其次核显；但前提是必须同时支持 graphics、present 和 swapchain。
    auto bestDevice = std::max_element(devices.begin(), devices.end(), [this](VkPhysicalDevice left, VkPhysicalDevice right) {
        return deviceScore(left) < deviceScore(right);
    });

    if (bestDevice == devices.end() || deviceScore(*bestDevice) < 0) {
        throw std::runtime_error("No suitable Vulkan physical device supports graphics, present, and swapchain.");
    }

    physicalDevice_ = *bestDevice;
    graphicsQueueFamilyIndex_ = findGraphicsPresentQueueFamily(physicalDevice_);

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &properties);

    std::cout << "Selected Vulkan device: " << properties.deviceName << '\n';
    std::cout << "Device type: " << deviceTypeName(properties.deviceType) << '\n';
    std::cout << "Graphics/present queue family: " << graphicsQueueFamilyIndex_ << '\n';
}

void VulkanContext::createLogicalDevice() {
    const float queuePriority = 1.0f;

    // 当前阶段只要一条 graphics+present 队列即可。
    // 后面加入异步 compute/transfer 时，再拆更多 queue family。
    const VkDeviceQueueCreateInfo queueCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = graphicsQueueFamilyIndex_,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority,
    };

    VkPhysicalDeviceFeatures deviceFeatures{};

    const VkDeviceCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCreateInfo,
        .enabledExtensionCount = static_cast<uint32_t>(std::size(RequiredDeviceExtensions)),
        .ppEnabledExtensionNames = RequiredDeviceExtensions,
        .pEnabledFeatures = &deviceFeatures,
    };

    vulkan_utils::checkVk(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_), "vkCreateDevice");
    vkGetDeviceQueue(device_, graphicsQueueFamilyIndex_, 0, &graphicsQueue_);

    loadDeviceDebugFunctions();
    setDebugObjectName(VK_OBJECT_TYPE_DEVICE, reinterpret_cast<uint64_t>(device_), "VulkanContext device");
}

void VulkanContext::loadDeviceDebugFunctions() {
    if (!validationEnabled_) {
        return;
    }

    // device 创建好以后，只查一次 device-level debug utils 函数指针。
    // 后面给任意 Vulkan 对象命名时直接复用它，避免反复 vkGetDeviceProcAddr。
    setDebugUtilsObjectName_ =
        reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            vkGetDeviceProcAddr(device_, "vkSetDebugUtilsObjectNameEXT"));
}

bool VulkanContext::validationLayerAvailable() const {
    uint32_t layerCount = 0;
    vulkan_utils::checkVk(vkEnumerateInstanceLayerProperties(&layerCount, nullptr), "vkEnumerateInstanceLayerProperties");

    std::vector<VkLayerProperties> layers(layerCount);
    vulkan_utils::checkVk(vkEnumerateInstanceLayerProperties(&layerCount, layers.data()), "vkEnumerateInstanceLayerProperties");

    return std::any_of(layers.begin(), layers.end(), [](const VkLayerProperties& layer) {
        return std::strcmp(layer.layerName, ValidationLayer) == 0;
    });
}

bool VulkanContext::instanceExtensionAvailable(const char* name) const {
    uint32_t extensionCount = 0;
    vulkan_utils::checkVk(
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr),
        "vkEnumerateInstanceExtensionProperties");

    std::vector<VkExtensionProperties> extensions(extensionCount);
    vulkan_utils::checkVk(
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data()),
        "vkEnumerateInstanceExtensionProperties");

    return std::any_of(extensions.begin(), extensions.end(), [name](const VkExtensionProperties& extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

bool VulkanContext::deviceSupportsRequiredExtensions(VkPhysicalDevice device) const {
    uint32_t extensionCount = 0;
    vulkan_utils::checkVk(vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr), "vkEnumerateDeviceExtensionProperties");

    std::vector<VkExtensionProperties> extensions(extensionCount);
    vulkan_utils::checkVk(vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data()), "vkEnumerateDeviceExtensionProperties");

    for (const char* requiredExtension : RequiredDeviceExtensions) {
        const bool found = std::any_of(extensions.begin(), extensions.end(), [requiredExtension](const VkExtensionProperties& extension) {
            return std::strcmp(extension.extensionName, requiredExtension) == 0;
        });

        if (!found) {
            return false;
        }
    }

    return true;
}

uint32_t VulkanContext::findGraphicsPresentQueueFamily(VkPhysicalDevice device) const {
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);

    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());

    for (uint32_t index = 0; index < familyCount; ++index) {
        VkBool32 presentSupported = VK_FALSE;
        vulkan_utils::checkVk(vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface_, &presentSupported), "vkGetPhysicalDeviceSurfaceSupportKHR");

        // 有些设备 graphics 和 present 分属不同队列。为了第一版保持简单，
        // 这里先选择同时支持两者的 queue family。
        const bool graphicsSupported = (families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        if (graphicsSupported && presentSupported) {
            return index;
        }
    }

    return InvalidQueueFamily;
}

int VulkanContext::deviceScore(VkPhysicalDevice device) const {
    const uint32_t queueFamily = findGraphicsPresentQueueFamily(device);
    if (queueFamily == InvalidQueueFamily || !deviceSupportsRequiredExtensions(device)) {
        return -1;
    }

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device, &properties);

    if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        return 100;
    }

    if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        return 50;
    }

    return 10;
}

std::vector<const char*> VulkanContext::requiredInstanceExtensions() const {
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    if (!glfwExtensions || glfwExtensionCount == 0) {
        throw std::runtime_error("GLFW did not report required Vulkan instance extensions.");
    }

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    if (validationEnabled_) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return extensions;
}
