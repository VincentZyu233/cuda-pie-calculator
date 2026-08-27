#pragma once

#include "pi_engine.hpp"

#include <cstdint>
#include <memory>

namespace pie {

struct ResourceStats {
    bool cpuAvailable = false;
    double cpuPercent = 0.0;
    std::uint64_t memoryUsedBytes = 0;
    std::uint64_t memoryTotalBytes = 0;
    bool gpuDetailsAvailable = false;
    unsigned gpuUtilizationPercent = 0;
    std::uint64_t gpuMemoryUsedBytes = 0;
    std::uint64_t gpuMemoryTotalBytes = 0;
    unsigned gpuTemperatureCelsius = 0;
};

class ResourceMonitor {
public:
    explicit ResourceMonitor(const GpuInfo& gpu);
    ~ResourceMonitor();

    ResourceMonitor(const ResourceMonitor&) = delete;
    ResourceMonitor& operator=(const ResourceMonitor&) = delete;

    ResourceStats sample();

private:
    struct NvmlApi;
    void openNvml(const GpuInfo& gpu);
    void closeNvml();
    std::unique_ptr<NvmlApi> nvml_;
    bool cpuBaselineReady_ = false;
    std::uint64_t previousCpuTotal_ = 0;
    std::uint64_t previousCpuIdle_ = 0;
};

std::string formatBytes(std::uint64_t bytes);

}  // namespace pie
