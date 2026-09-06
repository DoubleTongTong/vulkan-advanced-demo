#include "GlfwWindow.h"

#include <exception>
#include <iostream>

int main() {
    try {
        // 传入 1280x800 创建普通窗口；如果想铺满工作区，可以改成 0, 0。
        GlfwWindow window("GLFW Vulkan Demo", 1280, 800);
        window.run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
