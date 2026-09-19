#pragma once

#include "ui/IImGuiPanel.h"

class ImGuiDemoPanel final : public IImGuiPanel {
public:
    void draw() override;
};
