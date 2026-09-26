#pragma once

#include "ui/IImGuiPanel.h"
#include "ui/canvas/LineCanvas2D.h"
#include "ui/graph/ScrollingGraph.h"

class FrameTimer;

// 用 ImPlot 展示帧时长历史，并用 2D canvas 标出 60 FPS 帧预算。
class FrameGraphPanel final : public IImGuiPanel {
public:
    explicit FrameGraphPanel(const FrameTimer& timer) : timer_(timer) {}

    void draw() override;

private:
    void updateGraphRange();

    const FrameTimer& timer_;
    ScrollingGraph frameTimes_;
    LineCanvas2D canvas_;
    float graphMinimum_ = 0.0f;
    float graphMaximum_ = 0.0f;
};
