#include "SceneCamera.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/matrix.hpp>

#include <stdexcept>

SceneCamera::SceneCamera(
    GLFWwindow* window,
    FirstPersonCamera::Settings settings,
    Lens lens)
    : lens_(lens), camera_(settings), controller_(window, camera_) {
    if (lens_.verticalFieldOfViewDegrees <= 0.0f ||
        lens_.nearPlane <= 0.0f ||
        lens_.farPlane <= lens_.nearPlane) {
        throw std::invalid_argument("Scene camera lens settings are invalid.");
    }
}

void SceneCamera::update(
    float deltaSeconds,
    bool acceptKeyboard,
    bool acceptMouse) {
    controller_.update(deltaSeconds, acceptKeyboard, acceptMouse);
}

RenderView SceneCamera::makeRenderView(VkExtent2D extent) const {
    if (extent.width == 0 || extent.height == 0) {
        throw std::invalid_argument("Scene camera requires a non-empty render extent.");
    }

    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    glm::mat4 projection = glm::perspectiveRH_ZO(
        glm::radians(lens_.verticalFieldOfViewDegrees),
        aspect,
        lens_.nearPlane,
        lens_.farPlane);
    // GLFW 的屏幕坐标向下增长，Vulkan 的 NDC Y 轴需要在投影矩阵中翻转。
    projection[1][1] *= -1.0f;

    const glm::mat4 view = camera_.viewMatrix();
    const glm::mat4 viewProjection = projection * view;
    return {
        .view = view,
        .projection = projection,
        .viewProjection = viewProjection,
        .inverseViewProjection = glm::inverse(viewProjection),
        .cameraPosition = camera_.position(),
    };
}
