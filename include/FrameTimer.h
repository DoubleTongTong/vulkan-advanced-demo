#pragma once

#include <chrono>
#include <cstdint>
#include <stdexcept>

// 统一提供帧间隔与 FPS，整帧只读取一次稳定时钟。
class FrameTimer {
public:
    explicit FrameTimer(double fpsIntervalSeconds = 0.5)
        : fpsIntervalSeconds_(fpsIntervalSeconds) {
        if (fpsIntervalSeconds <= 0.0) {
            throw std::invalid_argument("FPS averaging interval must be positive.");
        }
        reset();
    }

    void reset() {
        previousFrameTime_ = Clock::now();
        elapsedSeconds_ = 0.0;
        deltaSeconds_ = 0.0f;
        renderedFrames_ = 0;
        fps_ = 0.0;
    }

    // 在每帧更新逻辑前调用；deltaSeconds() 在本帧内保持不变。
    void beginFrame() {
        const Clock::time_point now = Clock::now();
        const double elapsedSeconds = std::chrono::duration<double>(now - previousFrameTime_).count();
        previousFrameTime_ = now;
        deltaSeconds_ = static_cast<float>(elapsedSeconds);
        elapsedSeconds_ += elapsedSeconds;
    }

    // 跳过呈现的循环仍计入经过时间，但不会增加已渲染帧数。
    void finishFrame(bool frameRendered) {
        if (frameRendered) {
            ++renderedFrames_;
        }
        if (elapsedSeconds_ >= fpsIntervalSeconds_) {
            fps_ = renderedFrames_ / elapsedSeconds_;
            renderedFrames_ = 0;
            elapsedSeconds_ = 0.0;
        }
    }

    float deltaSeconds() const { return deltaSeconds_; }
    double fps() const { return fps_; }

private:
    using Clock = std::chrono::steady_clock;

    Clock::time_point previousFrameTime_{};
    double fpsIntervalSeconds_;
    double elapsedSeconds_ = 0.0;
    float deltaSeconds_ = 0.0f;
    uint32_t renderedFrames_ = 0;
    double fps_ = 0.0;
};
