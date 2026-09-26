#include "ui/FrameGraphPanel.h"

#include "FrameTimer.h"

#include <imgui.h>
#include <implot.h>

#include <algorithm>

namespace {

constexpr float FrameBudgetMilliseconds = 1000.0f / 60.0f;
constexpr float MinimumGraphSpan = 0.1f;
constexpr float LowerGraphPadding = 0.35f;
constexpr float UpperGraphPadding = 0.85f;
constexpr glm::vec4 RecentAverageColor{0.35f, 0.95f, 0.45f, 0.85f};
constexpr glm::vec4 FrameBudgetColor{1.0f, 0.75f, 0.20f, 0.85f};

} // namespace

void FrameGraphPanel::updateGraphRange() {
    // 用中间 80% 的帧时间建立局部坐标轴，在可读性和偶发尖峰之间保持平衡。
    const float sampleMinimum = frameTimes_.percentile(0.10f);
    const float sampleMaximum = frameTimes_.percentile(0.90f);
    const float sampleSpan = std::max(sampleMaximum - sampleMinimum, MinimumGraphSpan);
    const float desiredMinimum = std::max(0.0f, sampleMinimum - sampleSpan * LowerGraphPadding);
    const float highestSample = frameTimes_.maxValue();
    const float paddedPercentileMaximum = sampleMaximum + sampleSpan * UpperGraphPadding;
    // 让窗口内的最高帧时长下方占约三分之二高度，尖峰不会贴着图表顶边。
    const float maximumWithHeadroom = highestSample + (highestSample - desiredMinimum) * 0.5f;
    const float desiredMaximum = std::max(paddedPercentileMaximum, maximumWithHeadroom);
    const float currentSpan = graphMaximum_ - graphMinimum_;

    // 数据接近边缘时才重设范围；未触及边缘时保持刻度，避免曲线随每帧跳动。
    const bool rangeIsUninitialized = currentSpan <= 0.0f;
    const bool reachesRangeEdge = !rangeIsUninitialized &&
        (sampleMinimum < graphMinimum_ + currentSpan * 0.12f ||
         sampleMaximum > graphMaximum_ - currentSpan * 0.12f);
    const bool hasTooMuchEmptySpace = !rangeIsUninitialized &&
        (desiredMinimum > graphMinimum_ + currentSpan * 0.30f ||
         desiredMaximum < graphMaximum_ - currentSpan * 0.30f);
    if (rangeIsUninitialized || reachesRangeEdge || hasTooMuchEmptySpace) {
        graphMinimum_ = desiredMinimum;
        graphMaximum_ = desiredMaximum;
    }
}

void FrameGraphPanel::draw() {
    const float deltaSeconds = timer_.deltaSeconds();
    if (deltaSeconds > 0.0f) {
        frameTimes_.addPoint(deltaSeconds * 1000.0f);
    }
    updateGraphRange();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    constexpr float margin = 12.0f;
    constexpr ImVec2 size{300.0f, 164.0f};
    ImGui::SetNextWindowPos(
        {viewport->WorkPos.x + viewport->WorkSize.x - margin,
         viewport->WorkPos.y + margin + 56.0f},
        ImGuiCond_Always, {1.0f, 0.0f});
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.6f);

    constexpr ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("##FrameGraph", nullptr, windowFlags)) {
        ImGui::Text("Frame %.3f ms", frameTimes_.latestValue());
        ImGui::SameLine();
        ImGui::TextDisabled("Range %.3f - %.3f ms", graphMinimum_, graphMaximum_);
        ImGui::TextColored({1.0f, 0.75f, 0.20f, 1.0f}, "60 FPS budget: %.2f ms", FrameBudgetMilliseconds);
        ImGui::SameLine();
        ImGui::TextDisabled(FrameBudgetMilliseconds > graphMaximum_ ? "above graph" : "in graph");
        if (timer_.fps() > 0.0) {
            ImGui::TextColored(
                {RecentAverageColor.r, RecentAverageColor.g, RecentAverageColor.b, 1.0f},
                "Recent average: %.1f FPS", timer_.fps());
        }

        const ImVec2 graphSize = ImGui::GetContentRegionAvail();
        ImPlot::SetNextAxisLimits(
            ImAxis_X1,
            0.0,
            static_cast<double>(frameTimes_.capacity() - 1),
            ImPlotCond_Always);
        ImPlot::SetNextAxisLimits(
            ImAxis_Y1,
            graphMinimum_,
            graphMaximum_,
            ImPlotCond_Always);

        if (ImPlot::BeginPlot("##FrameTime", graphSize,
                ImPlotFlags_CanvasOnly | ImPlotFlags_NoInputs)) {
            ImPlot::SetupAxes(nullptr, nullptr,
                ImPlotAxisFlags_NoDecorations,
                ImPlotAxisFlags_NoDecorations);
            frameTimes_.plot("##FrameMilliseconds", {0.20f, 0.80f, 1.0f, 1.0f});

            const ImVec2 plotPosition = ImPlot::GetPlotPos();
            const ImVec2 plotSize = ImPlot::GetPlotSize();

            // 16.67 ms 是稳定 60 FPS 的帧时长预算。超出局部范围时贴在边缘提示方向。
            canvas_.clear();
            const float clampedBudget = std::clamp(
                FrameBudgetMilliseconds, graphMinimum_, graphMaximum_);
            const float budgetY = plotPosition.y + plotSize.y *
                (1.0f - (clampedBudget - graphMinimum_) /
                    (graphMaximum_ - graphMinimum_));
            canvas_.line(
                {plotPosition.x, budgetY},
                {plotPosition.x + plotSize.x, budgetY},
                FrameBudgetColor,
                2.0f);

            if (timer_.fps() > 0.0) {
                const float recentAverageMilliseconds =
                    static_cast<float>(1000.0 / timer_.fps());
                const float clampedAverage = std::clamp(
                    recentAverageMilliseconds, graphMinimum_, graphMaximum_);
                const float averageY = plotPosition.y + plotSize.y *
                    (1.0f - (clampedAverage - graphMinimum_) /
                        (graphMaximum_ - graphMinimum_));
                canvas_.line(
                    {plotPosition.x, averageY},
                    {plotPosition.x + plotSize.x, averageY},
                    RecentAverageColor,
                    2.0f);
            }
            // 必须在 EndPlot 前写入 plot draw list，才能使用 ImPlot 当前的裁剪区和绘制层级。
            canvas_.render(*ImPlot::GetPlotDrawList());
            ImPlot::EndPlot();
        }
    }
    ImGui::End();
}
