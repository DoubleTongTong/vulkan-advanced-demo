#include "ui/ComputedMeshPanel.h"

#include "VulkanImGuiOverlay.h"
#include "rendering/ComputedMeshCommandRecorder.h"

#include <imgui.h>

#include <array>
#include <string>

namespace {

constexpr std::array<TorusKnotParameters, 9> Knots{{
    {1, 1}, {2, 3}, {2, 5}, {2, 7}, {3, 4},
    {2, 9}, {3, 5}, {5, 8}, {8, 9},
}};

} // namespace

ComputedMeshPanel::ComputedMeshPanel(ComputedMeshCommandRecorder& recorder)
    : recorder_(recorder) {}

void ComputedMeshPanel::attach(VulkanImGuiOverlay& overlay) {
    textureId_ = overlay.registerTexture(recorder_.generatedTexture());
}

void ComputedMeshPanel::draw() {
    ImGui::SetNextWindowPos({12.0f, 12.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.86f);
    if (!ImGui::Begin("Torus knot controls")) {
        ImGui::End();
        return;
    }

    bool colored = recorder_.useColoredMesh();
    if (ImGui::Checkbox("Use colored mesh", &colored)) {
        recorder_.setUseColoredMesh(colored);
    }

    float speed = recorder_.animationSpeed();
    if (ImGui::SliderFloat("Morph speed", &speed, 0.0f, 2.0f, "%.2f")) {
        recorder_.setAnimationSpeed(speed);
    }
    ImGui::ProgressBar(recorder_.morphCoefficient(), {-1.0f, 0.0f}, "Morph");

    ImGui::SeparatorText("P / Q presets");
    for (size_t index = 0; index < Knots.size(); ++index) {
        const TorusKnotParameters knot = Knots[index];
        const std::string label =
            std::to_string(knot.p) + ", " + std::to_string(knot.q);
        if (ImGui::Button(label.c_str(), {72.0f, 0.0f})) {
            recorder_.requestKnot(knot);
        }
        if (index % 3 != 2) {
            ImGui::SameLine();
        }
    }

    ImGui::SeparatorText("Morph queue");
    const auto& queue = recorder_.morphQueue();
    for (size_t index = 0; index < queue.size(); ++index) {
        ImGui::Text(
            "%s P = %u, Q = %u",
            index == 0 ? "Current:" : "Next:   ",
            queue[index].p,
            queue[index].q);
    }

    if (!colored && textureId_ != 0) {
        ImGui::SeparatorText("Compute-generated texture");
        ImGui::Image(
            static_cast<ImTextureID>(textureId_),
            {224.0f, 224.0f});
    }

    ImGui::End();
}
