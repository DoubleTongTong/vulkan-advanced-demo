#pragma once

#include "ui/IImGuiPanel.h"

class FrameTimer;

class FpsPanel final : public IImGuiPanel {
public:
    explicit FpsPanel(const FrameTimer& timer) : timer_(timer) {}

    void draw() override;

private:
    const FrameTimer& timer_;
};
