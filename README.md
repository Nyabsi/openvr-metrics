# <img src="https://github.com/Nyabsi/openvr-metrics/blob/master/resources/icon.png" height="24" width="24"> OpenVR Metrics

<img width="1232" height="706" src="https://github.com/user-attachments/assets/88be974b-78c7-48ad-90e8-7c6f625448f2"/>

<br/>

OpenVR Metrics is a real-time VR performance monitoring tool designed for both developers and users who want deeper insight into application performance and device behavior within SteamVR. It provides per-process metrics, system resource visibility, and device status information through an accessible in-VR overlay.

## Download

> [!IMPORTANT]
> Linux will not have pre-packaged builds on GitHub. Different distributions may ship different versions of system libraries (libc, Vulkan, and so on), making developing a single portable Linux package unstable. The only supported method for receiving automated updates and a functioning Linux build is through [Steam](https://store.steampowered.com/app/4361360/OpenVR_Metrics/), where the program runs inside the **Steam Linux Runtime Soldier**, which standardizes the environment across distributions.

To download the application, select the preferred download for your operating system below:

- [Windows x64 (Installer)](https://github.com/Nyabsi/openvr-metrics/releases/latest/download/openvr-metrics-win32-x64-installer.exe)
- [Windows x64 (Portable)](https://github.com/Nyabsi/openvr-metrics/releases/latest/download/openvr-metrics-win32-x64-portable.exe)

## Features

#### Performance Monitoring

Gain detailed insight into how individual applications impact system performance:

* CPU and GPU frame time metrics
* Real-time FPS display
* Reprojected and dropped frame counters

#### Resource Monitoring

Track system resource usage on a per-application basis:

* CPU and GPU utilization[^1]
* Dedicated and shared VRAM usage[^1]
* Video encoding utilization[^1]

#### Process List

View applications currently consuming system resources within SteamVR.

#### Device Battery Monitoring

* Display battery levels for connected VR devices
* Assign tracker roles to easily identify individual devices

#### Accessibility and Overlay Customization

* Choose left-hand or right-hand overlay placement
* Adjust overlay scale and mounting position (Above, Wrist, or Below)

#### SteamVR Resolution Adjustment

Modify SteamVR resolution directly from within OpenVR Metrics.

#### Display Color Adjustment

* Apply color filters and brightness controls universally across supported devices[^2]


[^1]: *GPU related metrics monitoring is only supported on Windows as of right now*
[^2]: *The following features are only available on SteamVR devices with native display connections (e.g., HDMI or DisplayPort)*

## Building

> [!NOTE]
> You need [Vulkan SDK](https://vulkan.lunarg.com/) to build this project, make sure you have it downloaded before proceeding

First clone the repository as recursive

```sh
git clone https://github.com/Nyabsi/openvr-metrics.git --recursive
```

Then you can build it from Visual Studio on Windows but if you're targeting Linux you can use this for development:

```sh
cmake -B build && cmake --build build
```

And now you can find the built results from the `bin/` folder on the project root

## License

This project is licensed under `Source First License 1.1` which can be found from the root of this project named `LICENSE.md`
