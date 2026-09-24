#include "ui/FpsPanel.h"

#include "FrameTimer.h"

#include <imgui.h>

void FpsPanel::draw() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float margin = 12.0f;
    // 用较长的占位文本定宽，数字变化时 HUD 不会左右跳动。
    const float width = ImGui::CalcTextSize("Frame 99999.9 ms").x + style.WindowPadding.x * 2.0f;
    const float height = ImGui::GetTextLineHeight() * 2.0f + style.ItemSpacing.y + style.WindowPadding.y * 2.0f;
    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - margin, viewport->WorkPos.y + margin),
        ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.6f);

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("##FPS", nullptr, flags)) {
        if (timer_.fps() > 0.0) {
            ImGui::Text("FPS   %.1f", timer_.fps());
            ImGui::Text("Frame %.1f ms", 1000.0 / timer_.fps());
        } else {
            ImGui::TextUnformatted("FPS   --");
            ImGui::TextUnformatted("Frame -- ms");
        }
    }
    ImGui::End();
}
