#pragma once

#include "ui/IImGuiPanel.h"

class FramesPerSecondCounter;

class FpsPanel final : public IImGuiPanel {
public:
    explicit FpsPanel(const FramesPerSecondCounter& counter) : counter_(counter) {}

    void draw() override;

private:
    const FramesPerSecondCounter& counter_;
};
