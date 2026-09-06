#pragma once

#include <cstdint>

struct GLFWwindow;

class GlfwWindow {
public:
    // width/height 传 0 时，窗口会铺满主显示器工作区，但避开任务栏。
    GlfwWindow(const char* title, uint32_t width = 1280, uint32_t height = 800);
    ~GlfwWindow();

    GlfwWindow(const GlfwWindow&) = delete;
    GlfwWindow& operator=(const GlfwWindow&) = delete;

    void run();

    GLFWwindow* handle() const;
    uint32_t width() const;
    uint32_t height() const;

private:
    GLFWwindow* window_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
};
