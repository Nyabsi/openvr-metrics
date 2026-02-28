#pragma once

#include <string>
#include <stdexcept>

#include <openvr.h>
#include <SDL3/SDL.h>

#include <helper/FormatHelper.h>

inline auto OpenVRInit(vr::EVRApplicationType type) -> void
{
    vr::EVRInitError result = {};
    VR_Init(&result, type);

    // if VRApplication_Background is specified when trying to launch
    // and SteamVR is not running, it will throw VRInitError_Init_NoServerForBackgroundApp (121)
    if (result == vr::VRInitError_Init_NoServerForBackgroundApp)
        throw std::runtime_error(format_helper::Format("This application requires SteamVR to be running to start!"));

    if (result == vr::VRInitError_Init_HmdNotFound)
        throw std::runtime_error(format_helper::Format("SteamVR was running but headset was not found."));
}

inline auto OpenVRManifestInstalled(const char* appKey) -> bool
{
    return vr::VRApplications()->IsApplicationInstalled(appKey);
}

inline auto GetCurrentGamePid() -> uint32_t {
	return vr::VRApplications()->GetCurrentSceneProcessId();
}

inline auto TrackerPropStringToString(const std::string& name_unformatted)
{
    if (name_unformatted.find("vive_tracker_left_foot") != std::string::npos)
        return "Left Foot";
    else if (name_unformatted.find("vive_tracker_right_foot") != std::string::npos)
        return "Right Foot";
    else if (name_unformatted.find("vive_tracker_left_shoulder") != std::string::npos)
        return "Left Shoulder";
    else if (name_unformatted.find("vive_tracker_right_shoulder") != std::string::npos)
        return "Right Shoulder";
    else if (name_unformatted.find("vive_tracker_left_elbow") != std::string::npos)
        return "Left Elbow";
    else if (name_unformatted.find("vive_tracker_right_elbow") != std::string::npos)
        return "Right Elbow";
    else if (name_unformatted.find("vive_tracker_left_knee") != std::string::npos)
        return "Left Knee";
    else if (name_unformatted.find("vive_tracker_right_knee") != std::string::npos)
        return "Right Knee";
    else if (name_unformatted.find("vive_tracker_waist") != std::string::npos)
        return "Waist";
    else if (name_unformatted.find("vive_tracker_chest") != std::string::npos)
        return "Chest";
    else if (name_unformatted.find("vive_tracker_camera") != std::string::npos)
        return "Camera";
    else if (name_unformatted.find("vive_tracker_keyboard") != std::string::npos)
        return "Keyboard";
    else if (name_unformatted.find("vive_tracker_handed") != std::string::npos)
        return "Handed Tracker";
    return "Generic Tracker";
}

inline auto OpenVRManifestInstall() -> void
{
    std::string manifestPath = {};
    manifestPath += SDL_GetCurrentDirectory();
    manifestPath += "manifest.vrmanifest";

    vr::EVRApplicationError result = vr::VRApplications()->AddApplicationManifest(manifestPath.data());
    if (result > vr::VRApplicationError_None)
        throw std::runtime_error(format_helper::Format("Failed to add manifest from \"{}\" ({})", manifestPath, static_cast<int>(result)));
}

class VrTrackedDeviceProperties {
  public:
    [[maybe_unused]] static auto FromDeviceIndex(uint32_t deviceIndex) -> VrTrackedDeviceProperties {
        return VrTrackedDeviceProperties{deviceIndex};
    }

    [[nodiscard]] auto Handle() const -> vr::TrackedDeviceIndex_t { return handle; }

    [[maybe_unused]] auto CheckConnection() const -> void {
        if (!vr::VRSystem()->IsTrackedDeviceConnected(handle))
            throw std::runtime_error("The device must be connected to use VrTrackedDeviceProperties!");
    }

    [[maybe_unused]] auto GetString(const vr::ETrackedDeviceProperty property) const -> std::string { 
        vr::ETrackedPropertyError result = {};
        std::vector<char> buffer(vr::k_unMaxPropertyStringSize);
        auto buffer_len = vr::VRSystem()->GetStringTrackedDeviceProperty(handle, property, buffer.data(), vr::k_unMaxPropertyStringSize, &result);
        if (result != vr::TrackedProp_Success || buffer_len == 0) {
            throw std::runtime_error(format_helper::Format(
                "Failed to get string prop \"{}\" for {} (err={})",
                static_cast<int>(property),
                static_cast<int>(handle),
                static_cast<int>(result)
            ));
        }

        return buffer.data();
    }

    [[maybe_unused]] auto GetBool(const vr::ETrackedDeviceProperty property) -> bool {
        vr::ETrackedPropertyError result = {};
        auto value = vr::VRSystem()->GetBoolTrackedDeviceProperty(handle, property, &result);
        if (result > vr::TrackedProp_Success)
            throw std::runtime_error(
                format_helper::Format(
                    "Failed to get bool prop \"{}\" for {} ({})",
                    static_cast<int>(property),
                    static_cast<int>(handle),
                    static_cast<int>(result)
                ));
        return value;
    }

    [[maybe_unused]] auto GetFloat(const vr::ETrackedDeviceProperty property) -> float {
        vr::ETrackedPropertyError result = {};
        auto value = vr::VRSystem()->GetFloatTrackedDeviceProperty(handle, property, &result);
        if (result > vr::TrackedProp_Success)
            throw std::runtime_error(
                format_helper::Format(
                    "Failed to get float prop \"{}\" for {} ({})",
                    static_cast<int>(property),
                    static_cast<int>(handle),
                    static_cast<int>(result)
                ));
        return value;
    }

    [[maybe_unused]] auto GetInt32(const vr::ETrackedDeviceProperty property) -> int32_t {
        vr::ETrackedPropertyError result = {};
        auto value = vr::VRSystem()->GetInt32TrackedDeviceProperty(handle, property, &result);
        if (result > vr::TrackedProp_Success)
            throw std::runtime_error(
                format_helper::Format(
                    "Failed to get int32 prop \"{}\" for {} ({})",
                    static_cast<int>(property),
                    static_cast<int>(handle),
                    static_cast<int>(result)
                ));
        return value;
    }

    // TODO: implement
    // [[maybe_unused]] auto GetArray(const vr::ETrackedDeviceProperty property) -> void { }

  private:
    explicit VrTrackedDeviceProperties(const vr::TrackedDeviceIndex_t handle) : handle{handle} {}

    vr::TrackedDeviceIndex_t handle;
};