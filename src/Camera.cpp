#include "Camera.h"

#include <GLFW/glfw3.h>

#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/quaternion_common.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

FirstPersonCamera::FirstPersonCamera(Settings settings)
    : settings_(settings) {
    if (settings_.dampingSeconds <= 0.0f ||
        settings_.maxSpeed <= 0.0f ||
        settings_.fastMultiplier <= 0.0f) {
        throw std::invalid_argument("First-person camera settings must be positive.");
    }
    reset();
}

void FirstPersonCamera::update(float deltaSeconds, const Input& input) {
    if (input.reset) {
        reset();
    }

    // 调试器暂停或窗口拖动后可能出现很大的帧间隔，限制它以免相机突然飞走。
    const float delta = std::clamp(deltaSeconds, 0.0f, 0.1f);
    if (input.rotate) {
        if (wasRotating_) {
            const glm::vec2 mouseDelta = input.mousePosition - previousMousePosition_;
            const glm::quat rotation(glm::vec3(
                settings_.mouseSpeed * mouseDelta.y,
                settings_.mouseSpeed * mouseDelta.x,
                0.0f));
            orientation_ = glm::normalize(rotation * orientation_);

            // 重新使用世界 up 构造朝向，避免连续拖动逐渐产生不期望的翻滚。
            const glm::mat4 view = viewMatrix();
            const glm::vec3 forward = -glm::vec3(view[0][2], view[1][2], view[2][2]);
            lookAt(position_, position_ + forward);
        }
        previousMousePosition_ = input.mousePosition;
    }
    wasRotating_ = input.rotate;

    const glm::mat4 view = viewMatrix();
    const glm::vec3 forward = -glm::vec3(view[0][2], view[1][2], view[2][2]);
    const glm::vec3 right = glm::vec3(view[0][0], view[1][0], view[2][0]);
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));

    glm::vec3 acceleration(0.0f);
    if (input.forward) acceleration += forward;
    if (input.backward) acceleration -= forward;
    if (input.left) acceleration -= right;
    if (input.right) acceleration += right;
    if (input.up) acceleration += up;
    if (input.down) acceleration -= up;

    if (glm::dot(acceleration, acceleration) > 0.0f) {
        acceleration = glm::normalize(acceleration);
        const float speedMultiplier = input.fast ? settings_.fastMultiplier : 1.0f;
        velocity_ += acceleration * settings_.acceleration * speedMultiplier * delta;

        const float maxSpeed = settings_.maxSpeed * speedMultiplier;
        if (glm::length(velocity_) > maxSpeed) {
            velocity_ = glm::normalize(velocity_) * maxSpeed;
        }
    } else {
        // 指数阻尼不依赖帧率，松开按键后速度会平滑衰减。
        velocity_ *= std::exp(-delta / settings_.dampingSeconds);
    }

    position_ += velocity_ * delta;
}

void FirstPersonCamera::reset() {
    position_ = settings_.position;
    velocity_ = glm::vec3(0.0f);
    wasRotating_ = false;
    lookAt(settings_.position, settings_.target);
}

glm::mat4 FirstPersonCamera::viewMatrix() const {
    const glm::mat4 translation = glm::translate(glm::mat4(1.0f), -position_);
    return glm::mat4_cast(orientation_) * translation;
}

glm::vec3 FirstPersonCamera::position() const {
    return position_;
}

void FirstPersonCamera::lookAt(const glm::vec3& position, const glm::vec3& target) {
    position_ = position;
    orientation_ = glm::normalize(glm::quat_cast(glm::lookAt(position, target, settings_.up)));
}

GlfwCameraController::GlfwCameraController(GLFWwindow* window, FirstPersonCamera& camera)
    : window_(window), camera_(camera) {
    if (!window_) {
        throw std::invalid_argument("Camera controller requires a GLFW window.");
    }
}

GlfwCameraController::~GlfwCameraController() {
    if (window_ && cursorCaptured_) {
        glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}

void GlfwCameraController::update(
    float deltaSeconds,
    bool acceptKeyboard,
    bool acceptMouse) {
    FirstPersonCamera::Input input;
    const bool focused = glfwGetWindowAttrib(window_, GLFW_FOCUSED) == GLFW_TRUE;
    const bool rotate =
        focused && acceptMouse &&
        glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

    if (rotate != cursorCaptured_) {
        // UE 风格飞行视角：右键进入鼠标捕获，离开窗口边缘后依旧能连续转向。
        glfwSetInputMode(window_, GLFW_CURSOR, rotate ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        if (glfwRawMouseMotionSupported()) {
            glfwSetInputMode(window_, GLFW_RAW_MOUSE_MOTION, rotate ? GLFW_TRUE : GLFW_FALSE);
        }
        cursorCaptured_ = rotate;
    }

    // UE 编辑器中，WASD/QE 只在按住右键的飞行视角里移动相机。
    if (rotate && acceptKeyboard) {
        input.forward = glfwGetKey(window_, GLFW_KEY_W) == GLFW_PRESS;
        input.backward = glfwGetKey(window_, GLFW_KEY_S) == GLFW_PRESS;
        input.left = glfwGetKey(window_, GLFW_KEY_A) == GLFW_PRESS;
        input.right = glfwGetKey(window_, GLFW_KEY_D) == GLFW_PRESS;
        input.up = glfwGetKey(window_, GLFW_KEY_E) == GLFW_PRESS;
        input.down = glfwGetKey(window_, GLFW_KEY_Q) == GLFW_PRESS;
        input.fast =
            glfwGetKey(window_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
            glfwGetKey(window_, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    }

    const bool resetPressed =
        focused && acceptKeyboard &&
        glfwGetKey(window_, GLFW_KEY_HOME) == GLFW_PRESS;
    input.reset = resetPressed && !resetWasPressed_;
    resetWasPressed_ = resetPressed;

    double mouseX = 0.0;
    double mouseY = 0.0;
    glfwGetCursorPos(window_, &mouseX, &mouseY);
    int width = 1;
    int height = 1;
    glfwGetWindowSize(window_, &width, &height);
    input.mousePosition = {
        static_cast<float>(mouseX / std::max(width, 1)),
        1.0f - static_cast<float>(mouseY / std::max(height, 1)),
    };
    input.rotate = rotate;

    camera_.update(deltaSeconds, input);
}
