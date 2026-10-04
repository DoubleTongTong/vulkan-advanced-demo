#include "ui/TessellationPanel.h"

#include "rendering/TessellatedDuckCommandRecorder.h"

#include <imgui.h>

void TessellationPanel::draw() {
    float scale = recorder_.tessellationScale();
    ImGui::SetNextWindowBgAlpha(0.75f);
    if (ImGui::Begin("Tessellation", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::SliderFloat("Scale", &scale, 0.5f, 2.0f, "%.2f")) {
            recorder_.setTessellationScale(scale);
        }
        ImGui::TextUnformatted("Move the camera to see distance-based LOD.");
    }
    ImGui::End();
}
