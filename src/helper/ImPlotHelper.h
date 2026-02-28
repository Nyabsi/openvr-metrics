#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <imgui.h>
#include <implot.h>

#include <helper/ImHelper.h>
#include <overlay/controller/ControllerOverlay.h>

namespace ImPlotHelper {

enum class PlotBandColor : uint8_t {
    Green,
    Orange,
    Yellow,
    Red,
    Magenta,
    Purple,
    LightBlue,
    PinkishRed,
};

inline auto GetPlotBandColor(PlotBandColor color) -> ImVec4
{
    switch (color) {
        case PlotBandColor::Orange:
            return Color_Orange;
        case PlotBandColor::Yellow:
            return Color_Yellow;
        case PlotBandColor::Red:
            return Color_Red;
        case PlotBandColor::Magenta:
            return Color_Magenta;
        case PlotBandColor::Purple:
            return Color_Purple;
        case PlotBandColor::LightBlue:
            return Color_LightBlue;
        case PlotBandColor::PinkishRed:
            return Color_PinkishRed;
        case PlotBandColor::Green:
        default:
            return Color_Green;
    }
}

inline auto GetCpuPlotBandFromFlags(uint32_t flags) -> PlotBandColor
{
    if (flags & FrameTimeInfo_Flags_Reprojecting)
        return PlotBandColor::Orange;
    if (flags & FrameTimeInfo_Flags_MotionSmoothingEnabled)
        return PlotBandColor::Yellow;
    if (flags & FrameTimeInfo_Flags_OneThirdFramePresented)
        return PlotBandColor::Red;
    if (flags & FrameTimeInfo_Flags_Frame_LateStart)
        return PlotBandColor::Red;
    if (flags & FrameTimeInfo_Flags_Frame_Dropped)
        return PlotBandColor::Magenta;
    if (flags & FrameTimeInfo_Flags_Frame_Cpu_Stalled)
        return PlotBandColor::Purple;
    if (flags & FrameTimeInfo_Flags_PredictedAhead)
        return PlotBandColor::LightBlue;
    if (flags & FrameTimeInfo_Flags_Frame_Throttled)
        return PlotBandColor::PinkishRed;
    return PlotBandColor::Green;
}

inline auto GetGpuPlotBandFromFlags(uint32_t flags) -> PlotBandColor
{
    if (flags & FrameTimeInfo_Flags_Reprojecting)
        return PlotBandColor::Orange;
    if (flags & FrameTimeInfo_Flags_MotionSmoothingEnabled)
        return PlotBandColor::Yellow;
    if (flags & FrameTimeInfo_Flags_OneThirdFramePresented)
        return PlotBandColor::Red;
    if (flags & FrameTimeInfo_Flags_Frame_Dropped)
        return PlotBandColor::Magenta;
    return PlotBandColor::Green;
}

inline auto PlotShadedRuns(const char* label_prefix,
                           const std::vector<float>& x,
                           const std::vector<float>& y,
                           const std::vector<PlotBandColor>& segment_colors) -> void
{
    const int segment_count = static_cast<int>(segment_colors.size());
    for (int segment_start = 0; segment_start < segment_count;) {
        const auto color_key = segment_colors[segment_start];

        int segment_end = segment_start;
        while ((segment_end + 1) < segment_count && segment_colors[segment_end + 1] == color_key) {
            ++segment_end;
        }

        ImVec4 color = GetPlotBandColor(color_key);
        color.w *= 0.5f;

        const int point_count = (segment_end - segment_start) + 2;
        ImPlot::PushStyleColor(ImPlotCol_Fill, ImGui::ColorConvertFloat4ToU32(color));
        ImPlot::PlotShaded((std::string(label_prefix) + std::to_string(segment_start)).c_str(), &x[segment_start], &y[segment_start], point_count, 0.0f);
        ImPlot::PopStyleColor();

        segment_start = segment_end + 1;
    }
}

}
