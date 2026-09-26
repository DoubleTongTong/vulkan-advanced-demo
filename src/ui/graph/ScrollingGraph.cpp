#include "ui/graph/ScrollingGraph.h"

#include <implot.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

ScrollingGraph::ScrollingGraph(std::size_t maxPoints) : maxPoints_(maxPoints) {
    if (maxPoints_ == 0) {
        throw std::invalid_argument("Scrolling graph capacity must be positive.");
    }
}

void ScrollingGraph::addPoint(float value) {
    points_.push_back(value);
    if (points_.size() > maxPoints_) {
        points_.erase(points_.begin());
    }
}

bool ScrollingGraph::empty() const {
    return points_.empty();
}

std::size_t ScrollingGraph::capacity() const {
    return maxPoints_;
}

float ScrollingGraph::latestValue() const {
    return points_.empty() ? 0.0f : points_.back();
}

float ScrollingGraph::minValue() const {
    return points_.empty() ? 0.0f : *std::min_element(points_.begin(), points_.end());
}

float ScrollingGraph::maxValue() const {
    return points_.empty() ? 0.0f : *std::max_element(points_.begin(), points_.end());
}

float ScrollingGraph::percentile(float fraction) const {
    if (points_.empty()) {
        return 0.0f;
    }

    std::vector<float> sortedPoints = points_;
    std::sort(sortedPoints.begin(), sortedPoints.end());
    const float index = fraction * static_cast<float>(sortedPoints.size() - 1);
    const std::size_t lowerIndex = static_cast<std::size_t>(index);
    const std::size_t upperIndex = std::min(lowerIndex + 1, sortedPoints.size() - 1);
    const float weight = index - static_cast<float>(lowerIndex);
    return sortedPoints[lowerIndex] * (1.0f - weight) + sortedPoints[upperIndex] * weight;
}

void ScrollingGraph::plot(const char* label, const ImVec4& color) const {
    if (points_.empty()) {
        return;
    }

    ImPlot::SetNextLineStyle(color, 2.0f);
    ImPlot::PlotLine(label, points_.data(), static_cast<int>(points_.size()));
}
