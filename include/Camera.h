#pragma once

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

struct GLFWwindow;

// 渲染代码只依赖相机结果，不关心相机由键鼠、动画还是脚本驱动。
class Camera {
public:
    virtual ~Camera() = default;

    virtual glm::mat4 viewMatrix() const = 0;
    virtual glm::vec3 position() const = 0;
};

class FirstPersonCamera final : public Camera {
public:
    struct Settings {
        glm::vec3 position{0.0f, 0.05f, 3.0f};
        glm::vec3 target{0.0f, 0.05f, 0.0f};
        glm::vec3 up{0.0f, 1.0f, 0.0f};
        float mouseSpeed = 4.0f;
        float acceleration = 12.0f;
        float dampingSeconds = 0.15f;
        float maxSpeed = 3.0f;
        float fastMultiplier = 4.0f;
    };

    struct Input {
        glm::vec2 mousePosition{0.0f};
        bool rotate = false;
        bool forward = false;
        bool backward = false;
        bool left = false;
        bool right = false;
        bool up = false;
        bool down = false;
        bool fast = false;
        bool reset = false;
    };

    explicit FirstPersonCamera(Settings settings = {});

    void update(float deltaSeconds, const Input& input);
    void reset();

    glm::mat4 viewMatrix() const override;
    glm::vec3 position() const override;

private:
    void lookAt(const glm::vec3& position, const glm::vec3& target);

    Settings settings_;
    glm::vec2 previousMousePosition_{0.0f};
    glm::vec3 position_{0.0f};
    glm::vec3 velocity_{0.0f};
    glm::quat orientation_{};
    bool wasRotating_ = false;
};

// GLFW 只负责采集输入，运动规律全部留在 FirstPersonCamera 中。
class GlfwCameraController {
public:
    GlfwCameraController(GLFWwindow* window, FirstPersonCamera& camera);
    ~GlfwCameraController();

    void update(
        float deltaSeconds,
        bool acceptKeyboard = true,
        bool acceptMouse = true);

private:
    GLFWwindow* window_ = nullptr;
    FirstPersonCamera& camera_;
    bool resetWasPressed_ = false;
    bool cursorCaptured_ = false;
};
