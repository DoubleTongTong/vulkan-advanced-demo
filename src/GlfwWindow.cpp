#include "GlfwWindow.h"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <stdexcept>

GlfwWindow::GlfwWindow(const char* title, uint32_t width, uint32_t height) {
    // GLFW 的错误信息默认不会直接打印出来，先装一个简单回调方便排查问题。
    glfwSetErrorCallback([](int error, const char* description) {
        std::printf("GLFW Error (%d): %s\n", error, description);
    });

    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW.");
    }

    // Vulkan 项目不需要 GLFW 创建 OpenGL 上下文。
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    const bool wantsWholeArea = width == 0 || height == 0;
    glfwWindowHint(GLFW_RESIZABLE, wantsWholeArea ? GLFW_FALSE : GLFW_TRUE);

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);

    int x = 0;
    int y = 0;
    int windowWidth = mode->width;
    int windowHeight = mode->height;

    if (wantsWholeArea) {
        // 工作区会避开 Windows 任务栏、macOS Dock 等系统占用区域。
        glfwGetMonitorWorkarea(monitor, &x, &y, &windowWidth, &windowHeight);
    } else {
        windowWidth = static_cast<int>(width);
        windowHeight = static_cast<int>(height);
    }

    window_ = glfwCreateWindow(windowWidth, windowHeight, title, nullptr, nullptr);
    if (!window_) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window.");
    }

    if (wantsWholeArea) {
        glfwSetWindowPos(window_, x, y);
    }

    // 创建后的尺寸才是最终可用尺寸，尤其在不同 DPI/平台上更稳。
    glfwGetWindowSize(window_, &windowWidth, &windowHeight);
    width_ = static_cast<uint32_t>(windowWidth);
    height_ = static_cast<uint32_t>(windowHeight);

    // 默认支持 Esc 退出，后续 demo 都能沿用这个基础行为。
    glfwSetKeyCallback(window_, [](GLFWwindow* window, int key, int, int action, int) {
        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
    });
}

GlfwWindow::~GlfwWindow() {
    if (window_) {
        glfwDestroyWindow(window_);
    }

    glfwTerminate();
}

void GlfwWindow::run() {
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();
    }
}

bool GlfwWindow::shouldClose() const {
    return glfwWindowShouldClose(window_);
}

void GlfwWindow::pollEvents() const {
    glfwPollEvents();
}

GLFWwindow* GlfwWindow::handle() const {
    return window_;
}

uint32_t GlfwWindow::width() const {
    return width_;
}

uint32_t GlfwWindow::height() const {
    return height_;
}
