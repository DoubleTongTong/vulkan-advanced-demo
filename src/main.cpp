#include "GlfwWindow.h"
#include "VulkanContext.h"

#include <exception>
#include <iostream>

int main() {
    try {
        // 传入 1280x800 创建普通窗口；如果想铺满工作区，可以改成 0, 0。
        GlfwWindow window("GLFW Vulkan Demo", 1280, 800);

        // 先完成 Vulkan 最基础的上下文创建：instance、surface、device 和 graphics queue。
        VulkanContext vulkan(window.handle());

        window.run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
