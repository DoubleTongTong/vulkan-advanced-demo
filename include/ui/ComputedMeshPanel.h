#pragma once

#include "ui/IImGuiPanel.h"

#include <cstdint>

class ComputedMeshCommandRecorder;
class VulkanImGuiOverlay;

// 只负责环面结参数控件；网格生成和动画状态仍由 Recorder 管理。
class ComputedMeshPanel final : public IImGuiPanel {
public:
    explicit ComputedMeshPanel(ComputedMeshCommandRecorder& recorder);

    void attach(VulkanImGuiOverlay& overlay) override;
    void draw() override;

private:
    ComputedMeshCommandRecorder& recorder_;
    uint32_t textureId_ = 0;
};
