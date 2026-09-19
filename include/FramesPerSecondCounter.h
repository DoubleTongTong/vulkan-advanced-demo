#pragma once

#include <chrono>
#include <cstdint>
#include <stdexcept>

// 按固定时间窗口统计已渲染帧数；跳过渲染的循环仍计入经过的时间。
class FramesPerSecondCounter {
public:
    explicit FramesPerSecondCounter(double intervalSeconds = 0.5) : intervalSeconds_(intervalSeconds) {
        if (intervalSeconds <= 0.0) {
            throw std::invalid_argument("FPS averaging interval must be positive.");
        }
    }

    void reset() {
        lastTick_ = Clock::now();
        elapsedSeconds_ = 0.0;
        renderedFrames_ = 0;
        fps_ = 0.0;
    }

    void tick(bool frameRendered) {
        const auto now = Clock::now();
        // 计时包含跳过渲染的循环，帧数只统计实际完成的渲染。
        elapsedSeconds_ += std::chrono::duration<double>(now - lastTick_).count();
        lastTick_ = now;
        if (frameRendered) {
            ++renderedFrames_;
        }
        if (elapsedSeconds_ >= intervalSeconds_) {
            fps_ = renderedFrames_ / elapsedSeconds_;
            renderedFrames_ = 0;
            elapsedSeconds_ = 0.0;
        }
    }

    double fps() const { return fps_; }

private:
    using Clock = std::chrono::steady_clock;

    Clock::time_point lastTick_ = Clock::now();
    double intervalSeconds_;
    double elapsedSeconds_ = 0.0;
    uint32_t renderedFrames_ = 0;
    double fps_ = 0.0;
};
