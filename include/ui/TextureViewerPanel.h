#pragma once

#include "ui/IImGuiPanel.h"

#include <cstdint>

class VulkanTexture2D;

class TextureViewerPanel final : public IImGuiPanel {
public:
    explicit TextureViewerPanel(const VulkanTexture2D& texture) : texture_(texture) {}

    void attach(VulkanImGuiOverlay& overlay) override;
    void draw() override;

private:
    const VulkanTexture2D& texture_;
    uint32_t textureId_ = 0;
};
