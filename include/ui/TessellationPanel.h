#pragma once

#include "ui/IImGuiPanel.h"

class TessellatedDuckCommandRecorder;

// 只负责编辑细分参数；实际 GPU 资源仍由 Recorder 管理。
class TessellationPanel final : public IImGuiPanel {
public:
    explicit TessellationPanel(TessellatedDuckCommandRecorder& recorder)
        : recorder_(recorder) {}

    void draw() override;

private:
    TessellatedDuckCommandRecorder& recorder_;
};
