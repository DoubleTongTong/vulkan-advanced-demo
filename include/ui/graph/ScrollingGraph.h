#pragma once

#include <cstddef>
#include <vector>

struct ImVec4;

// 保存固定数量的最近采样值，适合显示随时间向左滚动的性能曲线。
class ScrollingGraph {
public:
    explicit ScrollingGraph(std::size_t maxPoints = 240);

    void addPoint(float value);
    bool empty() const;
    std::size_t capacity() const;
    float latestValue() const;
    float minValue() const;
    float maxValue() const;
    float percentile(float fraction) const;
    void plot(const char* label, const ImVec4& color) const;

private:
    std::size_t maxPoints_ = 0;
    std::vector<float> points_;
};
