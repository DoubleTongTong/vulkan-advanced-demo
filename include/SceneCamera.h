#pragma once

#include "Camera.h"
#include "rendering/RenderFrameContext.h"

#include <vulkan/vulkan.h>

struct GLFWwindow;

// 统一管理场景相机的输入、运动和镜头参数；每帧只生成一次 RenderView。
class SceneCamera final {
public:
    struct Lens {
        float verticalFieldOfViewDegrees = 45.0f;
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
    };

    SceneCamera(
        GLFWwindow* window,
        FirstPersonCamera::Settings settings = {},
        Lens lens = {});

    void update(
        float deltaSeconds,
        bool acceptKeyboard = true,
        bool acceptMouse = true);
    RenderView makeRenderView(VkExtent2D extent) const;

private:
    Lens lens_;
    FirstPersonCamera camera_;
    GlfwCameraController controller_;
};
