#include "ui/canvas/LineCanvas2D.h"

#include <imgui.h>

void LineCanvas2D::clear() {
    lines_.clear();
}

void LineCanvas2D::line(
    const glm::vec2& from,
    const glm::vec2& to,
    const glm::vec4& color,
    float thickness) {
    lines_.push_back({.from = from, .to = to, .color = color, .thickness = thickness});
}

void LineCanvas2D::render(ImDrawList& drawList) const {
    for (const Line& lineData : lines_) {
        const ImU32 color = ImGui::ColorConvertFloat4ToU32({
            lineData.color.r,
            lineData.color.g,
            lineData.color.b,
            lineData.color.a,
        });
        drawList.AddLine(
            {lineData.from.x, lineData.from.y},
            {lineData.to.x, lineData.to.y},
            color,
            lineData.thickness);
    }
}
