#pragma once

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <vector>

struct ImDrawList;

// 在 CPU 侧收集屏幕空间线段；由调用方决定写入哪个 ImGui draw list。
class LineCanvas2D {
public:
    void clear();
    void line(
        const glm::vec2& from,
        const glm::vec2& to,
        const glm::vec4& color,
        float thickness = 1.0f);
    void render(ImDrawList& drawList) const;

private:
    struct Line {
        glm::vec2 from{0.0f};
        glm::vec2 to{0.0f};
        glm::vec4 color{1.0f};
        float thickness = 1.0f;
    };

    std::vector<Line> lines_;
};
