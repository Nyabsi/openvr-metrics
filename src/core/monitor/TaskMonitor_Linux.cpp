#include "TaskMonitor.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <cctype>
#include <filesystem>
#include <chrono>


TaskMonitor::TaskMonitor()
{
    process_list_.clear();
    process_map_.clear();

    last_total_jiffies_ = 0.0;
    last_process_jiffies_.clear();
    last_update_time_ = {};
}

auto TaskMonitor::Initialize() -> void
{
    Update();
}

auto TaskMonitor::Destroy() -> void
{
    process_list_.clear();
    process_map_.clear();
}

auto TaskMonitor::Update() -> void
{
    process_list_.clear();
    process_map_.clear();

    size_t total_mem = 0;
    {
        std::ifstream f("/proc/meminfo");
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("MemTotal:", 0) == 0) {
                std::istringstream iss(line);
                std::string key;
                size_t kb = 0;
                iss >> key >> kb;
                total_mem = kb * 1024ULL;
                break;
            }
        }
    }

    long total_jiffies = 0;
    {
        std::ifstream f("/proc/stat");
        std::string line;
        if (std::getline(f, line)) {
            std::istringstream iss(line);
            std::string cpu;
            iss >> cpu;
            long v;
            while (iss >> v) total_jiffies += v;
        }
    }

    std::unordered_map<uint32_t, long> curr_jiffies;

    for (const auto& entry : std::filesystem::directory_iterator("/proc")) {
        if (!entry.is_directory()) continue;

        const std::string& fname = entry.path().filename().string();
        if (!std::all_of(fname.begin(), fname.end(), ::isdigit)) continue;

        uint32_t pid = std::stoul(fname);
        std::filesystem::path p = entry.path();

        std::string name;
        std::ifstream(p / "comm") >> name;

        size_t rss_bytes = 0;
        {
            std::ifstream status(p / "status");
            std::string line;
            while (std::getline(status, line)) {
                if (line.rfind("VmRSS:", 0) == 0) {
                    std::istringstream iss(line);
                    std::string k;
                    size_t kb = 0;
                    iss >> k >> kb;
                    rss_bytes = kb * 1024ULL;
                    break;
                }
            }
        }

        auto [u, s] = ReadProcessJiffies(p / "stat");
        curr_jiffies[pid] = u + s;

        ProcessInfo info{};
        info.pid = pid;
        info.process_name = std::move(name);
        info.memory_usage = rss_bytes;
        info.memory_available = total_mem;

        process_list_[pid] = std::move(info);
        if (!process_list_[pid].process_name.empty()) {
            process_map_[process_list_[pid].process_name] = pid;
        }
    }

    long delta_total = total_jiffies - last_total_jiffies_;
    bool first = (last_total_jiffies_ == 0);

    for (auto& [pid, info] : process_list_) {
        long delta_proc = curr_jiffies[pid] - last_process_jiffies_[pid];

        info.cpu.total_cpu_usage = (first || delta_total <= 0) ? 0.0 : 100.0 * static_cast<double>(delta_proc) / delta_total;
        info.cpu.user_cpu_usage = 0.0;
        info.cpu.kernel_cpu_usage = 0.0;
    }

    last_total_jiffies_ = total_jiffies;
    last_process_jiffies_ = std::move(curr_jiffies);
    last_update_time_ = std::chrono::steady_clock::now();
}

auto TaskMonitor::GetProcessInfoByPid(uint32_t pid) -> ProcessInfo 
{
    return process_list_[pid];
}

auto TaskMonitor::ReadProcessJiffies(const std::filesystem::path& stat_path) -> std::pair<long, long>
{
    std::ifstream file(stat_path);
    if (!file.is_open()) 
        return {0, 0};

    std::string line;
    if (!std::getline(file, line)) 
        return {0, 0};

    size_t pos = line.rfind(')');
    if (pos == std::string::npos) return {0, 0};

    std::istringstream iss(line.substr(pos + 1));

    std::string token;
    // skip to utime (field 14)
    for (int i = 0; i < 11; ++i) {
        iss >> token;
    }

    long utime = 0, stime = 0;
    iss >> utime >> stime;
    return {utime, stime};
}