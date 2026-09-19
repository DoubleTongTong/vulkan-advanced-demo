#pragma once

class VulkanImGuiOverlay;

// 界面只描述控件；Vulkan 资源和绘制命令由 overlay 统一处理。
class IImGuiPanel {
public:
    virtual ~IImGuiPanel() = default;

    virtual void attach(VulkanImGuiOverlay&) {}
    virtual void draw() = 0;
};
