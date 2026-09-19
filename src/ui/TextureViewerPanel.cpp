#include "ui/TextureViewerPanel.h"

#include "VulkanImGuiOverlay.h"

#include <imgui.h>

void TextureViewerPanel::attach(VulkanImGuiOverlay& overlay) {
    textureId_ = overlay.registerTexture(texture_);
}

void TextureViewerPanel::draw() {
    ImGui::Begin("Texture Viewer");
    ImGui::Image(static_cast<ImTextureID>(textureId_), ImVec2(320, 320));
    ImGui::End();
}
