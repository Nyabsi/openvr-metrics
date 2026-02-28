#include "ControllerOverlay.h"

#include <algorithm>
#include <map>
#include <thread>
#include <ranges>
#include <math.h>

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <extension/ImGui/backends/imgui_impl_openvr.h>
#include <helper/ImHelper.h>
#include <implot.h>

#include <extension/OpenVR/VrUtils.h>

#define OVERLAY_KEY     "steam.overlay.4361360"
#define OVERLAY_NAME    "Glance Overlay"
#define OVERLAY_WIDTH   420
#define OVERLAY_HEIGHT  220

static float g_overlay_width = -1.0f;
static uint32_t g_last_index = vr::k_unTrackedDeviceIndexInvalid;
static vr::ETrackedControllerRole g_overlay_handedness = vr::TrackedControllerRole_Invalid;
static glm::vec3 g_position = {};
static glm::quat g_rotation = {};

ControllerOverlay::ControllerOverlay() : Overlay(OVERLAY_KEY, OVERLAY_NAME, vr::VROverlayType_World, OVERLAY_WIDTH, OVERLAY_HEIGHT)
{
    frame_time_ = {};
    refresh_rate_ = {};
    last_pid = {};
    cpu_frame_times_ = {};
    gpu_frame_times_ = {};
    tracked_devices_ = {};
    display_mode_ = {};
    overlay_scale_ = {};
    handedness_ = {};
    position_ = {};
    ss_scaling_enabled_ = false;
    ss_scale_ = {};
    total_dropped_frames_ = {};
    total_predicted_frames_ = {};
    total_throttled_frames_ = {};
    cpu_frame_time_ms_ = {};
    effective_cpu_frame_time_ms_ = {};
    gpu_frame_time_ms_ = {};
	cpu_frame_time_sample_ = {};
	gpu_frame_time_avg_ = {};
    current_fps_ = {};
    frame_index_ = {};
    bottleneck_flags_ = {};
    bottleneck_ = false;
    wireless_latency_ = {};
    transform_ = {};
    color_temperature_ = false;
    color_channel_red_ = {};
    color_channel_green_ = {};
    color_channel_blue_ = {};
    color_temp_ = {};
    color_brightness_ = {};
    colour_mask_ = {};
#ifndef __linux
    gpu_info_ = {};
    process_info_ = {};
#endif
    try {
        this->SetInputMethod(vr::VROverlayInputMethod_Mouse);
        this->EnableFlag(vr::VROverlayFlags_SendVRDiscreteScrollEvents);
        this->EnableFlag(vr::VROverlayFlags_EnableClickStabilization);
    }
    catch (const std::exception& ex) {
#ifdef _WIN32
        char error_message[512] = {};
        snprintf(error_message, 512, "Failed to initialize the overlay.\nReason: %s\r\n", ex.what());
        MessageBoxA(NULL, error_message, "Error!", MB_OK);
#endif
        printf("%s\n\n", ex.what());
        std::exit(EXIT_FAILURE);
    }

    ImPlot::CreateContext();

    task_monitor_.Initialize();
    settings_.Load();

    display_mode_ = static_cast<Overlay_DisplayMode>(settings_.DisplayMode());
    overlay_scale_ = settings_.OverlayScale();
    handedness_ = settings_.Handedness();
    position_ = settings_.Position();
    ss_scaling_enabled_ = settings_.SsScalingEnabled();
    color_temperature_ = settings_.PostProcessingEnabled();
    color_temp_ = settings_.ColorTemperature();
    color_brightness_ = settings_.ColorBrightness();

    colour_mask_ = new float[3] { 0.0f, 0.0f, 0.0f };

    ss_scale_ = vr::VRSettings()->GetFloat(vr::k_pch_SteamVR_Section, vr::k_pch_SteamVR_SupersampleScale_Float) * 100;
    color_channel_red_ = vr::VRSettings()->GetFloat(vr::k_pch_SteamVR_Section, vr::k_pch_SteamVR_HmdDisplayColorGainR_Float);
    color_channel_green_ = vr::VRSettings()->GetFloat(vr::k_pch_SteamVR_Section, vr::k_pch_SteamVR_HmdDisplayColorGainG_Float);
    color_channel_blue_ = vr::VRSettings()->GetFloat(vr::k_pch_SteamVR_Section, vr::k_pch_SteamVR_HmdDisplayColorGainB_Float);

    this->UpdateDeviceTransform();
}

auto ControllerOverlay::Render() -> bool
{
    if (!Overlay::Render())
        return false;

    if (this->DisplayMode() == Overlay_DisplayMode_Dashboard && !vr::VROverlay()->IsDashboardVisible())
        return false;

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplOpenVR_NewFrame();
    ImGui::NewFrame();


    if (!vr::VROverlay()->IsDashboardVisible())
        ImHelper::DrawCursor();

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 pos = ImVec2(io.DisplaySize.x, io.DisplaySize.y);

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(pos, ImGuiCond_Always);

    ImGui::Begin("OpenVR Metrics", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove);

    const ImVec2 mousePos = ImGui::GetIO().MousePos;
    if (!io.WantTextInput && (
        (!vr::VROverlay()->IsDashboardVisible() && (mousePos.x < 0.0f || mousePos.y < 0.0f)) ||
        (vr::VROverlay()->IsDashboardVisible() && !vr::VROverlay()->IsHoverTargetOverlay(this->Handle())))
    ) {
        ImGuiStyle& style = ImGui::GetStyle();

        ImGui::Indent(10.0f);
        uint32_t pid = GetCurrentGamePid();
        if (pid > 0) {
            // TODO: check if last_pid actually exists before doing reset, if game utilizes SteamVR Compositor GetCurrentGamePid might return wrong pid temporarily causing stat reset.
            if (last_pid != pid) {
                this->Reset();
                last_pid = pid;
            }

			process_info_ = task_monitor_.GetProcessInfoByPid(pid);
#ifndef __linux
            gpu_info_ = getCurrentlyUsedGpu(process_info_);
#endif
            ImGui::Text("Current Application: %s (%d)", process_info_.process_name.c_str(), pid);
        }
        else {
			ImGui::Text("Current Application: SteamVR Void");
        }
        ImGui::Unindent(10.0f);

        ImGui::Spacing();

        auto avail = ImGui::GetContentRegionAvail();
        auto childSize = ImVec2((avail.x / 2) - style.FramePadding.x, (avail.y / 2) - style.FramePadding.y);

        if (ImGui::BeginChild("##metrics_info", childSize, ImGuiChildFlags_None)) {
            if (ImGui::BeginTable("##cpu_frametime", 2, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::Indent(10.0f);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("CPU Frametime");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.1f ms", cpu_frame_time_sample_);
                ImGui::EndTable();
                ImGui::Unindent(10.0f);
            }

            ImVec2 plotSize = ImGui::GetContentRegionAvail();

            static double t = 0.0;
            t += ImGui::GetIO().DeltaTime;

            const int frame_count = static_cast<int>(refresh_rate_);
            const float frame_dt = 1.0f / refresh_rate_;
            const double history = frame_count * frame_dt;

            if (ImPlot::BeginPlot("##frameplotimer", plotSize,
                ImPlotFlags_CanvasOnly | ImPlotFlags_NoFrame)) {

                ImPlot::SetupAxes(
                    nullptr,
                    nullptr,
                    ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_NoTickMarks | ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoMenus | ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_NoSideSwitch | ImPlotAxisFlags_NoGridLines,
                    ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_NoTickMarks | ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoMenus | ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_NoSideSwitch | ImPlotAxisFlags_Lock
                );

                // HACK: frame_dt * 2.0f ensures history buffer is enough to fill the entire plot
                ImPlot::SetupAxisLimits(ImAxis_X1, -(history - (frame_dt * 2.0f)), 0.0, ImGuiCond_Always);
                ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, frame_time_ * 2.0, ImGuiCond_Always);

                static double y_ticks[1] = { frame_time_ };
                ImPlot::SetupAxisTicks(ImAxis_Y1, y_ticks, 1, nullptr, false);

                for (int i = 0; i < static_cast<int>(refresh_rate_) - 1; ++i) {

                    ImVec4 color;
                    if (cpu_frame_times_[i].flags & FrameTimeInfo_Flags_Reprojecting)
                        color = Color_Orange;
                    else if (cpu_frame_times_[i].flags & FrameTimeInfo_Flags_MotionSmoothingEnabled)
                        color = Color_Yellow;
                    else if (cpu_frame_times_[i].flags & FrameTimeInfo_Flags_OneThirdFramePresented)
                        color = Color_Red;
                    else if (cpu_frame_times_[i].flags & FrameTimeInfo_Flags_Frame_LateStart)
                        color = Color_Red;
                    else if (cpu_frame_times_[i].flags & FrameTimeInfo_Flags_Frame_Dropped)
                        color = Color_Magenta;
                    else if (cpu_frame_times_[i].flags & FrameTimeInfo_Flags_Frame_Cpu_Stalled)
                        color = Color_Purple;
                    else if (cpu_frame_times_[i].flags & FrameTimeInfo_Flags_PredictedAhead)
                        color = Color_LightBlue;
                    else if (cpu_frame_times_[i].flags & FrameTimeInfo_Flags_Frame_Throttled)
                        color = Color_PinkishRed;
                    else
                        color = Color_Green;

                    color.w *= 0.5f;

                    float seg_x[2] = { -i * frame_dt, -(i + 1) * frame_dt };
                    float seg_y[2] = { cpu_frame_times_[i].frametime, cpu_frame_times_[i + 1].frametime };
                    constexpr float seg_ybase[2] = { 0.0f, 0.0f };

                    ImPlot::PushStyleColor(ImPlotCol_Fill, ImGui::ColorConvertFloat4ToU32(color));
                    ImPlot::PlotShaded(("##shaded" + std::to_string(i)).c_str(), seg_x, seg_ybase, seg_y, 2);
                    ImPlot::PopStyleColor();
                }

                ImPlot::EndPlot();
            }

            ImGui::EndChild();
        }

        ImGui::SameLine();

        if (ImGui::BeginChild("##metrics_info2", childSize, ImGuiChildFlags_None)) {
            if (ImGui::BeginTable("##gpu_frametime", 2, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::Indent(10.0f);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("GPU Frametime");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.1f ms", gpu_frame_time_avg_);
                ImGui::Unindent(10.0f);
                ImGui::EndTable();
            }

            static double t = 0.0;
            t += ImGui::GetIO().DeltaTime;

            const int frame_count = static_cast<int>(refresh_rate_);
            const float frame_dt = 1.0f / refresh_rate_;
            const double history = frame_count * frame_dt;

            ImVec2 plotSize = ImGui::GetContentRegionAvail();
            if (ImPlot::BeginPlot("Frametime Spikes GPU", plotSize, ImPlotFlags_CanvasOnly | ImPlotFlags_NoFrame)) {
                ImPlot::SetupAxes(
                    nullptr,
                    nullptr,
                    ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoTickMarks | ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoMenus | ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_NoSideSwitch | ImPlotAxisFlags_NoGridLines,
                    ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoTickMarks | ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoMenus | ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_NoSideSwitch | ImPlotAxisFlags_Lock
                );

                ImPlot::SetupAxisLimits(ImAxis_X1, -(history - (frame_dt * 2.0f)), 0.0, ImGuiCond_Always);
                ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, frame_time_ * 2.0, ImGuiCond_Always);

                static double y_ticks[1] = { frame_time_ };
                ImPlot::SetupAxisTicks(ImAxis_Y1, y_ticks, 1, nullptr, false);

                for (int i = 0; i < static_cast<int>(refresh_rate_) - 1; ++i) {

                    ImVec4 color = {};

                    if (gpu_frame_times_[i].flags & FrameTimeInfo_Flags_Reprojecting)
                        color = Color_Orange;
                    else if (gpu_frame_times_[i].flags & FrameTimeInfo_Flags_MotionSmoothingEnabled)
                        color = Color_Yellow;
                    else if (gpu_frame_times_[i].flags & FrameTimeInfo_Flags_OneThirdFramePresented)
                        color = Color_Red;
                    else if (gpu_frame_times_[i].flags & FrameTimeInfo_Flags_Frame_Dropped)
                        color = Color_Magenta;
                    else
                        color = Color_Green;

                    color.w *= 0.5f;

                    float seg_x[2] = { -i * frame_dt, -(i + 1) * frame_dt };
                    float seg_y[2] = { gpu_frame_times_[i].frametime, gpu_frame_times_[i + 1].frametime };
                    constexpr float seg_ybase[2] = { 0.0f, 0.0f };

                    ImPlot::PushStyleColor(ImPlotCol_Fill, ImGui::ColorConvertFloat4ToU32(color));
                    ImPlot::PlotShaded(("##shaded" + std::to_string(i)).c_str(), seg_x, seg_ybase, seg_y, 2);
                    ImPlot::PopStyleColor();
                }

                ImPlot::EndPlot();
            }

            ImGui::EndChild();
        }

        avail = ImGui::GetContentRegionAvail();
        childSize = ImVec2((avail.x / 2) - style.FramePadding.x, avail.y - style.FramePadding.y);

        if (ImGui::BeginChild("##metrics_info3", childSize, ImGuiChildFlags_None)) {

            if (ImGui::BeginTable("##metrics_extra", 2, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::Indent(10.0f);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("CPU");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.1f %%", process_info_.cpu.total_cpu_usage);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("FPS");
                ImGui::TableSetColumnIndex(1);
                if (current_fps_ < refresh_rate_ / 2)
                    ImGui::TextColored(Color_Red, "%1.f", current_fps_);
                else if (current_fps_ < refresh_rate_)
                    ImGui::TextColored(Color_Orange, "%1.f", current_fps_);
                else
                    ImGui::Text("%1.f", current_fps_);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("Dropped");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(Color_Red, "%d Frames", total_dropped_frames_);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("Predicted");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(Color_Red, "%d Frames", total_predicted_frames_);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("Throttled");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(Color_Red, "%d Frames", total_throttled_frames_);

                ImGui::Unindent(10.0f);

                ImGui::EndTable();
            }

            ImGui::EndChild();
        }

        ImGui::SameLine();

        if (ImGui::BeginChild("##metrics_info4", childSize, ImGuiChildFlags_None)) {



            if (ImGui::BeginTable("##metrics_extra3", 2, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::Indent(10.0f);
#ifndef __linux
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("GPU");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.1f %%", gpuPercentage(gpu_info_));

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("D-VRAM");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text(
                    "%.0f MB (%.1f%%)",
                    gpu_info_.memory.dedicated_vram_usage / (1024.0f * 1024.0f),
                    gpu_info_.memory.dedicated_available > 0
                    ? (gpu_info_.memory.dedicated_vram_usage * 100.0f) / gpu_info_.memory.dedicated_available
                    : 0.0f
                );

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("S-VRAM");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text(
                    "%.0f MB (%.1f%%)",
                    gpu_info_.memory.shared_vram_usage / (1024.0f * 1024.0f),
                    gpu_info_.memory.shared_available > 0
                    ? (gpu_info_.memory.shared_vram_usage * 100.0f) / gpu_info_.memory.shared_available
                    : 0.0f
                );
#endif
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("RAM");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text(
                    "%.0f MB (%.1f%%)",
                    process_info_.memory_usage / (1024.0f * 1024.0f),
                    gpu_info_.memory.dedicated_available > 0
                    ? (process_info_.memory_usage * 100.0f) / process_info_.memory_available
                    : 0.0f
                );

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("Bottleneck");
                ImGui::TableSetColumnIndex(1);

                if (bottleneck_flags_ == BottleneckSource_Flags_None)
                {
                    ImGui::TextColored(Color_Green, "None");
                }
                else
                {
                    std::string label;

                    auto append = [&](const char* text)
                        {
                            if (!label.empty())
                                label += " & ";
                            label += text;
                        };

                    if (bottleneck_flags_ & BottleneckSource_Flags_Wireless)
                        append("Wireless");

                    if (bottleneck_flags_ & BottleneckSource_Flags_CPU)
                        append("CPU");

                    if (bottleneck_flags_ & BottleneckSource_Flags_GPU)
                        append("GPU");

                    ImGui::TextColored(Color_Orange, "%s", label.c_str());
                }

                ImGui::Unindent(10.0f);

                ImGui::EndTable();
            }

            ImGui::EndChild();
        }
    }
    else {
        if (ImGui::BeginTabBar("##settings")) {

            if (ImGui::BeginTabItem("Settings")) {
                if (ImGui::BeginTable("##cpu_frametime_table", 2, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("Show");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::SameLine();
                    if (ImGui::Button("Dashboard")) {
                        this->TriggerLaserMouseHapticVibration(0.005f, 150.0f, 1.0f);
                        display_mode_ = Overlay_DisplayMode_Dashboard;
						settings_.SetDisplayMode(display_mode_);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Always")) {
                        this->TriggerLaserMouseHapticVibration(0.005f, 150.0f, 1.0f);
                        display_mode_ = Overlay_DisplayMode_Always;
						settings_.SetDisplayMode(display_mode_);
                    }

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("Scale");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::SameLine();
                    if (ImGui::InputFloat("##overlay_scale", &overlay_scale_, 0.05f, 0.0f, "%.2f")) {
                        this->TriggerLaserMouseHapticVibration(0.005f, 150.0f, 1.0f);
						settings_.SetOverlayScale(overlay_scale_);
                    }

                    // Scale safe boundaries
                    if (overlay_scale_ < 0.10f)
                        overlay_scale_ = 0.10f;

                    if (overlay_scale_ > 1.0f)
                        overlay_scale_ = 1.0f;

                    static int selected_handedness = 0;
                    const char* handedness_types[] = { "Left Hand", "Right Hand" };

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("Handedness");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::SameLine();

                    if (ImGui::BeginCombo("##handedness", handedness_types[selected_handedness])) {
                        for (int i = 0; i < IM_ARRAYSIZE(handedness_types); i++) {
                            bool is_selected = (selected_handedness == i);
                            if (ImGui::Selectable(handedness_types[i], is_selected)) {
                                selected_handedness = i;
                                this->TriggerLaserMouseHapticVibration(0.005f, 150.0f, 1.0f);
                                handedness_ = selected_handedness + 1;
                                this->UpdateDeviceTransform();
								settings_.SetHandedness(handedness_);
                            }
                        }
                        ImGui::EndCombo();
                    }

                    static int selected_position = 0;
                    const char* positions[] = { "Above", "Below", "Wrist" };

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("Position");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::SameLine();

                    if (ImGui::BeginCombo("##position", positions[selected_position])) {
                        for (int i = 0; i < IM_ARRAYSIZE(positions); i++) {
                            bool is_selected = (selected_position == i);
                            if (ImGui::Selectable(positions[i], is_selected)) {
                                position_ = i;
                                this->TriggerLaserMouseHapticVibration(0.005f, 150.0f, 1.0f);
                                this->UpdateDeviceTransform();
                                selected_position = i;
								settings_.SetPosition(position_);
                            }
                        }
                        ImGui::EndCombo();
                    }

                    ImGui::EndTable();
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Alerts"))
            {
                static int active_expanded_section = -1;

                auto GetAlertHeaderColor = [](uint32_t flag_mask) -> ImVec4 {
                    if (flag_mask & FrameTimeInfo_Flags_Frame_Dropped)         
                        return Color_Magenta;
                    if (flag_mask & FrameTimeInfo_Flags_OneThirdFramePresented) 
                        return Color_Red;
                    if (flag_mask & FrameTimeInfo_Flags_Frame_LateStart)
                        return Color_Red;
                    if (flag_mask & FrameTimeInfo_Flags_Reprojecting)           
                        return Color_Orange;
                    if (flag_mask & FrameTimeInfo_Flags_MotionSmoothingEnabled) 
                        return Color_Yellow;
                    if (flag_mask & FrameTimeInfo_Flags_Frame_Cpu_Stalled)      
                        return Color_LightBlue;
                    if (flag_mask & FrameTimeInfo_Flags_Frame_Throttled)        
                        return Color_PinkishRed;
                    if (flag_mask & FrameTimeInfo_Flags_PredictedAhead)
                        return Color_LightBlue;
                    return Color_Green;
                };

                {
                    struct AlertCategory {
                        const char* name;
                        uint32_t    flag;
                    };

                    static const AlertCategory categories[] = {
                        { "Dropped Frames",     FrameTimeInfo_Flags_Frame_Dropped },
                        { "Reprojecting",       FrameTimeInfo_Flags_Reprojecting },
                        { "Motion Smoothing",   FrameTimeInfo_Flags_MotionSmoothingEnabled },
                        { "One Third Frame",    FrameTimeInfo_Flags_OneThirdFramePresented },
                        { "CPU Stalled",        FrameTimeInfo_Flags_Frame_Cpu_Stalled },
                        { "Frame Throttled",    FrameTimeInfo_Flags_Frame_Throttled },
                        { "Predicted Ahead",    FrameTimeInfo_Flags_PredictedAhead },
                        { "Late Start",         FrameTimeInfo_Flags_Frame_LateStart },
                    };

                    constexpr int num_categories = IM_ARRAYSIZE(categories);

                    if (active_expanded_section == -1)
                    {
                        for (int i = 0; i < num_categories; ++i)
                        {
                            const auto& cat = categories[i];

                            auto has_alerts = performance_alerts_ | std::views::filter([f = cat.flag](const PerformanceAlert& a) {
                                return (a.flags & f) != 0;
                            });

                            if (std::ranges::empty(has_alerts)) 
                                continue;

                            ImVec4 col = GetAlertHeaderColor(cat.flag);
                            ImGui::PushStyleColor(ImGuiCol_Text, col);

                            if (ImGui::Selectable(cat.name))
                                active_expanded_section = i;

                            ImGui::PopStyleColor();
                        }
                    }
                    else
                    {
                        const auto& cat = categories[active_expanded_section];

                        if (ImGui::Button("Back"))
                            active_expanded_section = -1;

                        ImGui::SameLine();
                        ImGui::TextUnformatted(cat.name);

                        ImGui::Separator();

                        if (ImGui::BeginChild("##FullSizeAlertContent", ImVec2(0, 0), true, ImGuiWindowFlags_AlwaysVerticalScrollbar))
                        {
                            auto filtered = performance_alerts_ | std::views::filter([f = cat.flag](const PerformanceAlert& a) {
                                return (a.flags & f) != 0;
                            });

                            int shown = 0;
                            for (const auto& alert : filtered)
                            {
                                shown++;

                                std::string count_str = (alert.count > 1) ? " (x" + std::to_string(alert.count) + ")" : "";
                                ImGui::TextWrapped("%s%s", alert.message.c_str(), count_str.c_str());

                                ImGui::Dummy(ImVec2(0, 10));
                                ImGui::Separator();
                            }
                        }
                        ImGui::EndChild();
                    }
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Devices")) {
                ImGui::BeginChild("process_list_scroller", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

                ImGuiTableFlags flags =
                    ImGuiTableFlags_Borders |
                    ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_SizingStretchProp;

                if (ImGui::BeginTable("device_list", 3, flags))
                {
                    ImGui::TableSetupColumn("ID");
                    ImGui::TableSetupColumn("Name");
                    ImGui::TableSetupColumn("Battery %");
                    ImGui::TableHeadersRow();

                    for (auto& device : tracked_devices_)
                    {
                        ImGui::TableNextRow();

                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("%lu", device.device_id);

                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%s", device.device_label.c_str());


                        ImGui::TableSetColumnIndex(2);
                        if (device.battery_percentage <= 0.2f && device.battery_percentage > 0.10f)
                            ImGui::TextColored(Color_Yellow, "%d%%", (int)(device.battery_percentage * 100));
                        else if (device.battery_percentage <= 0.10f)
                            ImGui::TextColored(Color_Red, "%d%%", (int)(device.battery_percentage * 100));
                        else
                            ImGui::Text("%d%%", (int)(device.battery_percentage * 100));
                    }

                    ImGui::EndTable();
                }

                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Display")) {
                
                static float last_ss_scale = -1.0f;
                static float channel_r = 0.0f;
                static float channel_g = 0.0f;
                static float channel_b = 0.0f;

                if (ImGui::BeginTable("##display_resolution", 2, ImGuiTableFlags_SizingStretchSame)) {

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    bool enabled = ImGui::Checkbox("Enable SS Scaling", &ss_scaling_enabled_);
                    if (ss_scaling_enabled_) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("Current Scale: %.0f%%", ss_scale_);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::SameLine();
                        ImGui::InputFloat("##overlay_scale", &ss_scale_, 10.0f, 0.0f, "%.0f %%");

                        if (ss_scale_ < 10.0f)
                            ss_scale_ = 10.0f;

                        if (ss_scale_ > 500.0f)
                            ss_scale_ = 500.0f;

                        if (last_ss_scale != ss_scale_) {
                            vr::VRSettings()->SetFloat(vr::k_pch_SteamVR_Section, vr::k_pch_SteamVR_SupersampleScale_Float, ss_scale_ / 100.0f);
                            last_ss_scale = ss_scale_;
							this->TriggerLaserMouseHapticVibration(0.005f, 150.0f, 1.0f);
                        }
                    }

					if (enabled != ss_scaling_enabled_) {
						settings_.SetSsScalingEnabled(ss_scaling_enabled_);
					}

                    ImGui::EndTable();
                }

                if (ImGui::BeginTable("##display_temparature", 2, ImGuiTableFlags_SizingStretchSame)) {

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Checkbox("Enable Color Grading", &color_temperature_);

                    if (color_temperature_) {

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("Temparature: %.0f K", color_temp_);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::SameLine();
                        if (ImGui::InputFloat("##color_temparature", &color_temp_, 1000.0f, 0.0f, "%.0f K")) {
                            this->TriggerLaserMouseHapticVibration(0.005f, 150.0f, 1.0f);
                        }

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("Brightess: %.0f %%", color_brightness_);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::SameLine();
                        if (ImGui::InputFloat("##color_temparature_strength", &color_brightness_, 10.0f, 0.0f, "%.0f %%")) {
                            this->TriggerLaserMouseHapticVibration(0.005f, 150.0f, 1.0f);
                        }

                        if (color_brightness_ < 10.0f)
                            color_brightness_ = 10.0f;

                        if (color_brightness_ > 300.0f)
                            color_brightness_ = 300.0f;

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("Colours: %d, %d, %d", static_cast<int>(colour_mask_[0]), static_cast<int>(colour_mask_[1]), static_cast<int>(colour_mask_[2]));
                        ImGui::TableSetColumnIndex(1);
                        ImGui::SameLine();
                        ImGui::ColorEdit3("##colour_overlay", colour_mask_, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoDragDrop);

                        // https://tannerhelland.com/2012/09/18/convert-temperature-rgb-algorithm-code.html
                        float temperature = std::clamp<float>(color_temp_, 1000.0f, 40000.0f) / 100.0f;

                        if (temperature <= 66.0f) {
                            color_channel_red_ = 1.0f; // 255
                        }
                        else {
                            color_channel_red_ = (std::clamp<float>(329.698727446f * static_cast<float>(std::pow((temperature - 60.0f), -0.1332047592f)), 0, 255) / 255.0f);
                        }

                        if (temperature <= 66.0f) {
                            color_channel_green_ = (std::clamp<float>(99.4708025861f * ::logf(temperature) - 161.1195681661f, 0, 255) / 255.0f);
                        }
                        else {
                            color_channel_green_ = (std::clamp<float>(288.1221695283f * static_cast<float>(std::pow((temperature - 60.0f), -0.0755148492f)), 0, 255) / 255.0f);
                        }

                        if (temperature >= 66.0f) {
                            color_channel_blue_ = 1.0f; // 255
                        }
                        else {
                            if (temperature <= 19.0f) {
                                color_channel_blue_ = 0.01f;
                            }
                            else {
                                color_channel_blue_ = (std::clamp<float>(138.5177312231f * ::logf(temperature - 10.0f) - 305.0447927307f, 0, 255) / 255.0f);
                            }
                        }

                        auto gammaCorrect = [](float channel) -> float {
                            return std::abs(channel <= 0.04045f ? channel / 12.92f : pow((channel + 0.055f) / 1.055f, 2.4f));
                        };

                        float r = gammaCorrect(color_channel_red_);
                        float g = gammaCorrect(color_channel_green_);
                        float b = gammaCorrect(color_channel_blue_);

                        if (colour_mask_[0] > 0.0f || colour_mask_[1] > 0.0f || colour_mask_[2] > 0.0f) {
                            float original_luminance = 0.2126f * r + 0.7152f * g + 0.0722f * b;

                            r *= colour_mask_[0];
                            g *= colour_mask_[1];
                            b *= colour_mask_[2];

                            float tinted_luminance = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                            if (tinted_luminance > 0.0f) {
                                float brightness_correction = original_luminance / tinted_luminance;
                                r *= brightness_correction;
                                g *= brightness_correction;
                                b *= brightness_correction;
                            }
                        }

                        float brightness_factor = (color_brightness_ / 100.0f);

                        auto toneMap = [](float c, float brightness_factor) -> float {
                            float boosted = c * brightness_factor;
                            return boosted / (1.0f + (boosted - 1.0f) * 0.5f);
                        };

                        r = toneMap(r, brightness_factor);
                        g = toneMap(g, brightness_factor);
                        b = toneMap(b, brightness_factor);

                        color_channel_red_ = r;
                        color_channel_green_ = g;
                        color_channel_blue_ = b;

                        if ((channel_r != color_channel_red_ || channel_g != color_channel_green_ || channel_b != color_channel_blue_)) {
                            vr::VRSettings()->SetFloat(vr::k_pch_SteamVR_Section, vr::k_pch_SteamVR_HmdDisplayColorGainR_Float, color_channel_red_);
                            vr::VRSettings()->SetFloat(vr::k_pch_SteamVR_Section, vr::k_pch_SteamVR_HmdDisplayColorGainG_Float, color_channel_green_);
                            vr::VRSettings()->SetFloat(vr::k_pch_SteamVR_Section, vr::k_pch_SteamVR_HmdDisplayColorGainB_Float, color_channel_blue_);
                            channel_r = color_channel_red_;
                            channel_g = color_channel_green_;
                            channel_b = color_channel_blue_;
                        }
                    }
                    else {
                        if (channel_r != 0.0f || channel_g != 0.0f || channel_b != 0.0f) {
                            vr::VRSettings()->SetFloat(vr::k_pch_SteamVR_Section, vr::k_pch_SteamVR_HmdDisplayColorGainR_Float, 1.0f);
                            vr::VRSettings()->SetFloat(vr::k_pch_SteamVR_Section, vr::k_pch_SteamVR_HmdDisplayColorGainG_Float, 1.0f);
                            vr::VRSettings()->SetFloat(vr::k_pch_SteamVR_Section, vr::k_pch_SteamVR_HmdDisplayColorGainB_Float, 1.0f);
                            channel_r = 0.0f;
                            channel_g = 0.0f;
                            channel_b = 0.0f;
                        }
                    }

                    ImGui::EndTable();
                }
                
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    
    ImGui::End();
    ImGui::Render();

    return true;
}

auto ControllerOverlay::Update() -> void
{
    Overlay::Update();

    vr::Compositor_FrameTiming timings = {};
    timings.m_nSize = sizeof(vr::Compositor_FrameTiming);

    bool newData = vr::VRCompositor()->GetFrameTiming(&timings, 0);

    if (newData) {

        cpu_frame_time_ms_ =
            timings.m_flCompositorRenderCpuMs +
            timings.m_flPresentCallCpuMs +
            timings.m_flWaitForPresentCpuMs +
            timings.m_flClientFrameIntervalMs +
			timings.m_flCompositorIdleCpuMs +
            timings.m_flSubmitFrameMs;

        gpu_frame_time_ms_ =
            timings.m_flTotalRenderGpuMs;

        if (timings.m_flTransferLatencyMs > 0.0f) {
            wireless_latency_ = timings.m_flTransferLatencyMs;
        }
        else if (timings.m_flCompositorIdleCpuMs >= 1.0f) {
            wireless_latency_ = timings.m_flCompositorIdleCpuMs;
        }
        else {
            wireless_latency_ = 0.0f;
        }

        uint32_t predicted_frames = VR_COMPOSITOR_ADDITIONAL_PREDICTED_FRAMES(timings);
        uint32_t throttled_frames = VR_COMPOSITOR_NUMBER_OF_THROTTLED_FRAMES(timings);

        FrameTimeInfo info_cpu = {};
        FrameTimeInfo info_gpu = {};

        if (timings.m_nNumDroppedFrames >= 1) {
            if (wireless_latency_ >= 15.0f) {
                cpu_frame_time_ms_ += timings.m_flCompositorIdleCpuMs;
                info_cpu.flags |= FrameTimeInfo_Flags_Frame_CompositorIdle;
            }
            else {
                if (cpu_frame_time_ms_ >= frame_time_) {
                    cpu_frame_time_ms_ = frame_time_ * 2;
                }
            }
            if (gpu_frame_time_ms_ >= frame_time_) {
                gpu_frame_time_ms_ = frame_time_ * 2;
            }
            info_gpu.flags |= FrameTimeInfo_Flags_Frame_Dropped;
            info_cpu.flags |= FrameTimeInfo_Flags_Frame_Dropped;

        }
        else {
            if (cpu_frame_time_ms_ > frame_time_) {
                if (timings.m_flClientFrameIntervalMs > frame_time_)
                    info_cpu.flags |= FrameTimeInfo_Flags_Frame_LateStart;
                else {
                    if (throttled_frames >= 2)
                        info_cpu.flags |= FrameTimeInfo_Flags_Frame_Throttled;
                    else if (predicted_frames >= 1) {
                        if (predicted_frames >= 2)
                            info_cpu.flags |= FrameTimeInfo_Flags_Frame_Cpu_Stalled;
                        else
                            info_cpu.flags |= FrameTimeInfo_Flags_PredictedAhead;
                    }
                }
            }
            else {
                if (throttled_frames >= 2 && predicted_frames >= 2)
                    info_cpu.flags |= FrameTimeInfo_Flags_OneThirdFramePresented;
            }
            if (timings.m_nNumFramePresents > 1) {
                if (timings.m_nNumMisPresented >= 2) {
                    info_gpu.flags |= FrameTimeInfo_Flags_OneThirdFramePresented;
                }
                else {
                    if (timings.m_nReprojectionFlags & vr::VRCompositor_ReprojectionAsync) {
                        if (timings.m_nReprojectionFlags & vr::VRCompositor_ReprojectionMotion) {
                            info_gpu.flags |= FrameTimeInfo_Flags_MotionSmoothingEnabled;
                        }
                        else {

                            info_gpu.flags |= FrameTimeInfo_Flags_Reprojecting;
                        }
                    }
                }
            }
        }

        effective_cpu_frame_time_ms_ = std::max(frame_time_, cpu_frame_time_ms_);
        if (!(info_cpu.flags & FrameTimeInfo_Flags_Frame_LateStart))
            effective_cpu_frame_time_ms_ *= throttled_frames > 0 ? (throttled_frames + 1) : 1;

        info_cpu.frametime = cpu_frame_time_ms_;
        cpu_frame_times_.data()[frame_index_] = info_cpu;
        info_gpu.frametime = gpu_frame_time_ms_;
        gpu_frame_times_.data()[frame_index_] = info_gpu;

        total_predicted_frames_ += predicted_frames;
        total_dropped_frames_ += timings.m_nNumDroppedFrames;
        total_throttled_frames_ += throttled_frames;

        static uint32_t stable_bottleneck_flags = BottleneckSource_Flags_None;

        static int gpu_trigger_frames = 0;
        static int gpu_clear_frames = 0;

        static int cpu_trigger_frames = 0;
        static int cpu_clear_frames = 0;

        static int wireless_trigger_frames = 0;
        static int wireless_clear_frames = 0;

        constexpr int kTriggerThreshold = 5;
        constexpr int kClearThreshold = 10;

        bool gpu_detected = false;
        bool cpu_detected = false;
        bool wireless_detected = false;

        if (wireless_latency_ >= 15.0f)
            wireless_detected = true;

        if (gpu_frame_times_[frame_index_].flags != 0 &&
            static_cast<int>(gpu_frame_time_ms_) != static_cast<int>(frame_time_))
            gpu_detected = true;

        if (cpu_frame_times_[frame_index_].flags != 0 &&
            static_cast<int>(cpu_frame_time_ms_) != static_cast<int>(frame_time_))
            cpu_detected = true;

        bool gpu_stable = (stable_bottleneck_flags & BottleneckSource_Flags_GPU) != 0;

        if (gpu_detected)
        {
            gpu_clear_frames = 0;

            if (!gpu_stable)
            {
                if (++gpu_trigger_frames >= kTriggerThreshold)
                {
                    stable_bottleneck_flags |= BottleneckSource_Flags_GPU;
                    gpu_trigger_frames = 0;
                }
            }
        }
        else
        {
            gpu_trigger_frames = 0;

            if (gpu_stable)
            {
                if (++gpu_clear_frames >= kClearThreshold)
                {
                    stable_bottleneck_flags &= ~BottleneckSource_Flags_GPU;
                    gpu_clear_frames = 0;
                }
            }
        }

        bool cpu_stable = (stable_bottleneck_flags & BottleneckSource_Flags_CPU) != 0;

        if (cpu_detected)
        {
            cpu_clear_frames = 0;

            if (!cpu_stable)
            {
                if (++cpu_trigger_frames >= kTriggerThreshold)
                {
                    stable_bottleneck_flags |= BottleneckSource_Flags_CPU;
                    cpu_trigger_frames = 0;
                }
            }
        }
        else
        {
            cpu_trigger_frames = 0;

            if (cpu_stable)
            {
                if (++cpu_clear_frames >= kClearThreshold)
                {
                    stable_bottleneck_flags &= ~BottleneckSource_Flags_CPU;
                    cpu_clear_frames = 0;
                }
        }
        else if (detected_flags != BottleneckSource_Flags_None) {
            if (detected_flags == last_detected_flags) {
                consecutive_bottleneck_frames++;
                if (consecutive_bottleneck_frames >= kTriggerThreshold) {
                    stable_bottleneck_flags = detected_flags;
                    consecutive_bottleneck_frames = 0;
                    consecutive_clear_frames = 0;
                }

        bool wireless_stable = (stable_bottleneck_flags & BottleneckSource_Flags_Wireless) != 0;

        if (wireless_detected)
        {
            wireless_clear_frames = 0;

            if (!wireless_stable)
            {
                if (++wireless_trigger_frames >= kTriggerThreshold)
                {
                    stable_bottleneck_flags |= BottleneckSource_Flags_Wireless;
                    wireless_trigger_frames = 0;
            }
            else {
                last_detected_flags = detected_flags;
                consecutive_bottleneck_frames = 1;
            }
        }
        else
        {
            wireless_trigger_frames = 0;

            if (wireless_stable)
            {
                if (++wireless_clear_frames >= kClearThreshold)
                {
                    stable_bottleneck_flags &= ~BottleneckSource_Flags_Wireless;
                    wireless_clear_frames = 0;
                }
            }
        }

        bottleneck_flags_ = stable_bottleneck_flags;
        bottleneck_ = (stable_bottleneck_flags != BottleneckSource_Flags_None);

        uint64_t now_ms = SDL_GetTicks();
        if (!performance_alerts_.empty() && (now_ms - performance_alerts_.back().timestamp > (3600ULL * 1000ULL))) 
        {
            performance_alerts_.clear();
        }

        uint32_t alert_flags = {};
        std::string message = {};

        if (((cpu_frame_times_[frame_index_].flags & FrameTimeInfo_Flags_Frame_CompositorIdle) && (cpu_frame_times_[frame_index_].flags & FrameTimeInfo_Flags_Frame_Dropped))) {
            message = "The frame was discarded because the compositor's idle threshold exceeded the overall frame time budget due to excessive network latency. This commonly occurs in wireless streaming setups that do not enforce server-side pacing.";
            alert_flags |= FrameTimeInfo_Flags_Frame_Dropped | FrameTimeInfo_Flags_Frame_CompositorIdle;
        }
        else if (cpu_frame_times_[frame_index_].flags & FrameTimeInfo_Flags_Frame_Dropped) {
            message = "The frame was dropped by the compositor for an unknown cause; this normally signals transitory lag spikes when the game switches context, such as loading screens; if the problem persists, it could be a GPU vendor driver issue.";
            alert_flags |= FrameTimeInfo_Flags_Frame_Dropped;
        }
        else if ((gpu_frame_times_[frame_index_].flags & FrameTimeInfo_Flags_Reprojecting)) {
            message = "The compositor reprojected the frame since your machine was unable to meet the frame time target; please adjust your rendering resolution or try changing in-game settings to decrease fidelity to ensure consistent frame time.";
            alert_flags |= FrameTimeInfo_Flags_Reprojecting;
        }
        else if ((gpu_frame_times_[frame_index_].flags & FrameTimeInfo_Flags_MotionSmoothingEnabled)) {
            message = "You have enabled Motion Smoothing, which interpolates between previous and upcoming frames when your system is unable to satisfy the frame time budget; if you dislike the ripple effect, you can disable Motion Smoothing in the SteamVR settings.";
            alert_flags |= FrameTimeInfo_Flags_MotionSmoothingEnabled;
        }
        else if (gpu_frame_times_[frame_index_].flags & FrameTimeInfo_Flags_OneThirdFramePresented) {
            message = "The compositor presented the same frame across three different vsync intervals, indicating severe frame pacing issues. This usually occurs when the GPU is unable to sustain the required frame rate for your headset's refresh rate.";
            alert_flags |= FrameTimeInfo_Flags_OneThirdFramePresented;
        }
        else if (cpu_frame_times_[frame_index_].flags & FrameTimeInfo_Flags_Frame_Throttled) {
            message = "The compositor throttled frame submission due to repeated missed frame deadlines. This can occur when the application consistently exceeds the available frame time budget.";
            alert_flags |= FrameTimeInfo_Flags_Frame_Throttled;
        }
        else if (cpu_frame_times_[frame_index_].flags & FrameTimeInfo_Flags_Frame_Cpu_Stalled) {
            message = "The CPU was unable to submit frames in time, causing a stall in the rendering pipeline. This typically indicates a CPU bottleneck or heavy simulation workload.";
            alert_flags |= FrameTimeInfo_Flags_Frame_Cpu_Stalled;
        }
        else if (cpu_frame_times_[frame_index_].flags & FrameTimeInfo_Flags_PredictedAhead) {
            message = "The compositor predicted ahead due to minor frame timing instability. While not necessarily visible, this may indicate early signs of performance pressure.";
            alert_flags |= FrameTimeInfo_Flags_PredictedAhead;
        }
        else if (cpu_frame_times_[frame_index_].flags & FrameTimeInfo_Flags_Frame_LateStart) {
            message = "The frame began rendering after its targeted CPU budget, but not late enough to result in a dropped frame. This occurs when the application exceeds its CPU frame budget but still fits within the ~3ms running-start buffer before vsync. It may also signal server-side frame pacing enforcement on certain streams (e.g. ALVR, PICO Connect).";
            alert_flags |= FrameTimeInfo_Flags_Frame_LateStart;
        }

        frame_index_ = (frame_index_ + 1) % static_cast<int>(refresh_rate_);

        if (!message.empty())
        {
            bool merged = false;

            for (auto it = performance_alerts_.rbegin(); it != performance_alerts_.rend(); ++it) 
            {
                if ((it->flags == alert_flags) && (now_ms - it->timestamp) < (3 * (60 * 1000)))
                {
                    it->count++;
                    it->timestamp = now_ms;
                    it->flags = alert_flags;
                    merged = true;
                    break;
                }
            }

            if (!merged) {
                performance_alerts_.push_back(PerformanceAlert{
                    .timestamp = now_ms,
                    .flags = alert_flags,
                    .message = std::move(message),
                    .count = 1
                });
            }
        }
    }

    static double fps_counter = 0.0;
    if (ImGui::GetTime() - fps_counter >= 0.2f) {
        cpu_frame_time_sample_ = cpu_frame_time_ms_;
        gpu_frame_time_avg_ = gpu_frame_time_ms_;
        current_fps_ = (effective_cpu_frame_time_ms_ > 0.0f) ? 1000.0f / effective_cpu_frame_time_ms_ : 0.0f;
        fps_counter = ImGui::GetTime();
    }

    static double last_time = 0.0;
    if (ImGui::GetTime() - last_time >= 1.0f) {
        task_monitor_.Update();
        last_time = ImGui::GetTime();
    }

    switch (this->DisplayMode()) {
        case Overlay_DisplayMode_Always:
        {
            if (!this->IsVisible() && this->ShouldRender())
                this->Show();
            break;
        }
        case Overlay_DisplayMode_Dashboard:
        {
            if (this->IsVisible() && !vr::VROverlay()->IsDashboardVisible() && this->ShouldRender())
                this->Hide();

            if (!this->IsVisible() && vr::VROverlay()->IsDashboardVisible() && this->ShouldRender())
                this->Show();
            break;
        }
        default:
            break;
    }

    const auto scale = this->OverlayScale();
    if (g_overlay_width != scale) {
        this->SetWidth(scale);
        g_overlay_width = scale;
    }


    const auto transform = this->Transform();
    const auto handedness = static_cast<vr::ETrackedControllerRole>(this->Handedness());
    const auto hand_index = vr::VRSystem()->GetTrackedDeviceIndexForControllerRole(handedness);

    const glm::vec3 position = transform.position;
    const glm::quat rotation = transform.rotation;
    if (
        g_overlay_handedness != handedness ||
        (g_position != position && g_rotation != rotation) ||
        (g_last_index == vr::k_unTrackedDeviceIndexInvalid && hand_index != vr::k_unTrackedDeviceIndexInvalid)
    ) {
        this->SetTransformDeviceRelative(handedness, position, rotation);
        g_overlay_handedness = handedness;
        g_position = position;
        g_rotation = rotation;

        if (g_last_index == vr::k_unTrackedDeviceIndexInvalid)
            g_last_index = hand_index;
    }
}

auto ControllerOverlay::Destroy() -> void
{
    delete[] colour_mask_;
    colour_mask_ = nullptr;

    task_monitor_.Destroy();

    ImPlot::DestroyContext();
}

auto ControllerOverlay::Reset() -> void
{
    cpu_frame_times_.resize(static_cast<int>(refresh_rate_));
    gpu_frame_times_.resize(static_cast<int>(refresh_rate_));
    
    std::fill(cpu_frame_times_.begin(), cpu_frame_times_.end(), FrameTimeInfo{});
    std::fill(gpu_frame_times_.begin(), gpu_frame_times_.end(), FrameTimeInfo{});

    performance_alerts_.clear();

    frame_index_ = 0;

    total_predicted_frames_ = 0;
    total_dropped_frames_ = 0;
    total_throttled_frames_ = 0;
}

auto ControllerOverlay::SetFrameTime(float refresh_rate) -> void
{
    frame_time_ = 1000.0f / refresh_rate;
    refresh_rate_ = refresh_rate;

    this->Reset();
}

auto ControllerOverlay::UpdateBatteryPercentageForDeviceById(uint32_t device_id) -> void
{
    auto it = std::find_if(tracked_devices_.begin(), tracked_devices_.end(), [device_id](const TrackedDevice& a) { return a.device_id == device_id; });
    if (it != tracked_devices_.end()) {
        auto c_properties = VrTrackedDeviceProperties::FromDeviceIndex(device_id);
        int32_t device_type = c_properties.GetInt32(vr::Prop_DeviceClass_Int32);

        std::string name = { "-" };

        if (device_type == vr::TrackedDeviceClass_HMD) {
            name = "Headset";
        }

        else if (device_type == vr::TrackedDeviceClass_Controller) {
            int32_t controller_hint = c_properties.GetInt32(vr::Prop_ControllerRoleHint_Int32);
            name = controller_hint == vr::TrackedControllerRole_LeftHand ? "Left Controller" : "Right Controller";
        }

        else if (device_type == vr::TrackedDeviceClass_GenericTracker) {
            std::string controller_type = c_properties.GetString(vr::Prop_ControllerType_String);
            name = TrackerPropStringToString(controller_type);
        }

        if (c_properties.GetBool(vr::Prop_DeviceProvidesBatteryStatus_Bool)) {
            it->battery_percentage = c_properties.GetFloat(vr::Prop_DeviceBatteryPercentage_Float);
        }
        else {
            tracked_devices_.erase(it);
        }
    }
}

auto ControllerOverlay::AddMonitoredDeviceById(uint32_t device_id) -> void
{
    auto it = std::find_if(tracked_devices_.begin(), tracked_devices_.end(), [device_id](const TrackedDevice& a) { return a.device_id == device_id; });
    
    try {
        auto c_properties = VrTrackedDeviceProperties::FromDeviceIndex(device_id);
        if (it == tracked_devices_.end() && c_properties.GetBool(vr::Prop_DeviceProvidesBatteryStatus_Bool)) {
            int32_t device_type = c_properties.GetInt32(vr::Prop_DeviceClass_Int32);

            std::string name = { "-" };

            if (device_type == vr::TrackedDeviceClass_HMD) {
                name = "Headset";
            }

            else if (device_type == vr::TrackedDeviceClass_Controller) {
                int32_t controller_hint = c_properties.GetInt32(vr::Prop_ControllerRoleHint_Int32);
                name = controller_hint == vr::TrackedControllerRole_LeftHand ? "Left Controller" : "Right Controller";
            }

            else if (device_type == vr::TrackedDeviceClass_GenericTracker) {
                std::string controller_type = c_properties.GetString(vr::Prop_ControllerType_String);
                name = TrackerPropStringToString(controller_type);
            }

            TrackedDevice device =
            {
                .device_id = device_id,
                .device_label = name,
                .battery_percentage = c_properties.GetFloat(vr::Prop_DeviceBatteryPercentage_Float)
            };

            tracked_devices_.push_back(device);
        }
    } catch (...) { }
}

auto ControllerOverlay::RemoveMonitoredDeviceById(uint32_t device_id) -> void
{
    auto it = std::find_if(tracked_devices_.begin(), tracked_devices_.end(), [device_id](const TrackedDevice& a) { return a.device_id == device_id; });
    if (it != tracked_devices_.end()) {
        tracked_devices_.erase(it);
    }
}

auto ControllerOverlay::UpdateDeviceTransform() -> void
{
    glm::vec3 position = {};
    glm::quat rotation = {};

    if (handedness_ == vr::TrackedControllerRole_LeftHand) {

        switch (position_)
        {
        case 0: {
            position = { -0.15, 0, 0.10 };
            rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 1, 0)) * glm::angleAxis(-glm::half_pi<float>(), glm::vec3(1, 0, 0));
            rotation *= glm::angleAxis(glm::radians(10.0f), glm::vec3(0, 1, 0));
            rotation = glm::normalize(rotation);
            break;
        }
        case 1: {
            position = { 0, 0, 0.25 };
            rotation = glm::angleAxis(glm::pi<float>(), glm::vec3(0, 0, 1)) * glm::angleAxis(glm::pi<float>(), glm::vec3(0, 1, 0)) * glm::angleAxis(glm::half_pi<float>(), glm::vec3(1, 0, 0));
            rotation *= glm::angleAxis(glm::radians(10.0f), glm::vec3(0, 1, 0));
            rotation = glm::normalize(rotation);
            break;
        }
        case 2: {
            position = { -0.10, 0, 0.10 };
            rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 0, 1)) * glm::angleAxis(glm::pi<float>(), glm::vec3(0, 0, 1)) * glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 1, 0)) * glm::angleAxis(glm::half_pi<float>(), glm::vec3(1, 0, 0));
            rotation *= glm::angleAxis(glm::radians(10.0f), glm::vec3(0, 1, 0));
            rotation = glm::normalize(rotation);
            break;
        }
        default:
            break;
        }
    }

    else if (handedness_ == vr::TrackedControllerRole_RightHand) {
        switch (position_)
        {
        case 0: {
            position = { 0.15, 0, 0.10 };
            rotation = glm::angleAxis(-glm::pi<float>(), glm::vec3(0, 0, 1)) * glm::angleAxis(-glm::half_pi<float>(), glm::vec3(0, 1, 0)) * glm::angleAxis(glm::half_pi<float>(), glm::vec3(1, 0, 0));
            rotation *= -glm::angleAxis(glm::radians(10.0f), glm::vec3(0, 1, 0));
            rotation = glm::normalize(rotation);
            break;
        }
        case 1: {
            position = { 0, 0, 0.25 };
            rotation = glm::angleAxis(-glm::pi<float>(), glm::vec3(0, 0, 1)) * glm::angleAxis(glm::pi<float>(), glm::vec3(0, 1, 0)) * glm::angleAxis(glm::half_pi<float>(), glm::vec3(1, 0, 0));
            rotation *= -glm::angleAxis(glm::radians(10.0f), glm::vec3(0, 1, 0));
            rotation = glm::normalize(rotation);
            break;
        }
        case 2: {
            position = { 0.10, 0, 0.10 };
            rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 0, 1)) * glm::angleAxis(glm::pi<float>(), glm::vec3(0, 0, 1)) * glm::angleAxis(-glm::half_pi<float>(), glm::vec3(0, 1, 0)) * glm::angleAxis(-glm::half_pi<float>(), glm::vec3(1, 0, 0));
            rotation *= -glm::angleAxis(glm::radians(10.0f), glm::vec3(0, 1, 0));
            rotation = glm::normalize(rotation);
            break;
        }
        default:
            break;
        }
    }

    transform_ = { position, rotation };
}
