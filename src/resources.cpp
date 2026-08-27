#include "resources.hpp"

#include <nvml.h>

#include <algorithm>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#include <fstream>
#endif

namespace pie {
namespace {

#if defined(_WIN32)
std::uint64_t fileTimeValue(const FILETIME& time) {
    ULARGE_INTEGER value{};
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return value.QuadPart;
}
#endif

template <typename Function>
Function resolveSymbol(void* library, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<Function>(GetProcAddress(reinterpret_cast<HMODULE>(library), name));
#else
    return reinterpret_cast<Function>(dlsym(library, name));
#endif
}

void closeLibrary(void* library) {
    if (library == nullptr) {
        return;
    }
#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(library));
#else
    dlclose(library);
#endif
}

void* openLibrary() {
#if defined(_WIN32)
    return reinterpret_cast<void*>(LoadLibraryA("nvml.dll"));
#else
    return dlopen("libnvidia-ml.so.1", RTLD_LAZY | RTLD_LOCAL);
#endif
}

#if !defined(_WIN32)
bool readLinuxCpu(std::uint64_t& total, std::uint64_t& idle) {
    std::ifstream stat("/proc/stat");
    std::string label;
    std::uint64_t user = 0;
    std::uint64_t nice = 0;
    std::uint64_t system = 0;
    std::uint64_t idleValue = 0;
    std::uint64_t ioWait = 0;
    std::uint64_t irq = 0;
    std::uint64_t softIrq = 0;
    std::uint64_t steal = 0;
    if (!(stat >> label >> user >> nice >> system >> idleValue >> ioWait >> irq >> softIrq >> steal) || label != "cpu") {
        return false;
    }
    total = user + nice + system + idleValue + ioWait + irq + softIrq + steal;
    idle = idleValue + ioWait;
    return true;
}

void readLinuxMemory(std::uint64_t& used, std::uint64_t& total) {
    std::ifstream meminfo("/proc/meminfo");
    std::string key;
    std::uint64_t value = 0;
    std::string unit;
    std::uint64_t available = 0;
    while (meminfo >> key >> value >> unit) {
        if (key == "MemTotal:") {
            total = value * 1024ULL;
        } else if (key == "MemAvailable:") {
            available = value * 1024ULL;
        }
    }
    used = total > available ? total - available : 0;
}
#endif

}  // namespace

struct ResourceMonitor::NvmlApi {
    using Init = nvmlReturn_t (*)(void);
    using Shutdown = nvmlReturn_t (*)(void);
    using GetDevice = nvmlReturn_t (*)(unsigned int, nvmlDevice_t*);
    using GetUtilization = nvmlReturn_t (*)(nvmlDevice_t, nvmlUtilization_t*);
    using GetMemory = nvmlReturn_t (*)(nvmlDevice_t, nvmlMemory_t*);
    using GetTemperature = nvmlReturn_t (*)(nvmlDevice_t, nvmlTemperatureSensors_t, unsigned int*);

    ~NvmlApi() {
        if (initialized && shutdown != nullptr) {
            shutdown();
        }
        closeLibrary(library);
    }

    void* library = nullptr;
    nvmlDevice_t device = nullptr;
    Init init = nullptr;
    Shutdown shutdown = nullptr;
    GetDevice getDevice = nullptr;
    GetUtilization getUtilization = nullptr;
    GetMemory getMemory = nullptr;
    GetTemperature getTemperature = nullptr;
    bool initialized = false;
};

ResourceMonitor::ResourceMonitor(const GpuInfo& gpu) {
    openNvml(gpu);
}

ResourceMonitor::~ResourceMonitor() {
    closeNvml();
}

void ResourceMonitor::openNvml(const GpuInfo& gpu) {
    if (!gpu.available) {
        return;
    }

    auto api = std::make_unique<NvmlApi>();
    api->library = openLibrary();
    if (api->library == nullptr) {
        return;
    }

    api->init = resolveSymbol<NvmlApi::Init>(api->library, "nvmlInit_v2");
    api->shutdown = resolveSymbol<NvmlApi::Shutdown>(api->library, "nvmlShutdown");
    api->getDevice = resolveSymbol<NvmlApi::GetDevice>(api->library, "nvmlDeviceGetHandleByIndex_v2");
    api->getUtilization = resolveSymbol<NvmlApi::GetUtilization>(api->library, "nvmlDeviceGetUtilizationRates");
    api->getMemory = resolveSymbol<NvmlApi::GetMemory>(api->library, "nvmlDeviceGetMemoryInfo");
    api->getTemperature = resolveSymbol<NvmlApi::GetTemperature>(api->library, "nvmlDeviceGetTemperature");
    if (api->init == nullptr || api->shutdown == nullptr || api->getDevice == nullptr || api->getUtilization == nullptr || api->getMemory == nullptr || api->getTemperature == nullptr) {
        return;
    }
    if (api->init() != NVML_SUCCESS) {
        return;
    }
    api->initialized = true;
    if (api->getDevice(static_cast<unsigned>(gpu.deviceIndex), &api->device) != NVML_SUCCESS) {
        return;
    }
    nvml_ = std::move(api);
}

void ResourceMonitor::closeNvml() {
    nvml_.reset();
}

ResourceStats ResourceMonitor::sample() {
    ResourceStats stats;

#if defined(_WIN32)
    FILETIME idleTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};
    if (GetSystemTimes(&idleTime, &kernelTime, &userTime) != 0) {
        const std::uint64_t idle = fileTimeValue(idleTime);
        const std::uint64_t total = fileTimeValue(kernelTime) + fileTimeValue(userTime);
        if (cpuBaselineReady_ && total > previousCpuTotal_) {
            const std::uint64_t deltaTotal = total - previousCpuTotal_;
            const std::uint64_t deltaIdle = idle - previousCpuIdle_;
            stats.cpuPercent = 100.0 * (1.0 - static_cast<double>(deltaIdle) / static_cast<double>(deltaTotal));
            stats.cpuAvailable = true;
        }
        previousCpuTotal_ = total;
        previousCpuIdle_ = idle;
        cpuBaselineReady_ = true;
    }
    MEMORYSTATUSEX memoryStatus{};
    memoryStatus.dwLength = sizeof(memoryStatus);
    if (GlobalMemoryStatusEx(&memoryStatus) != 0) {
        stats.memoryTotalBytes = memoryStatus.ullTotalPhys;
        stats.memoryUsedBytes = memoryStatus.ullTotalPhys - memoryStatus.ullAvailPhys;
    }
#else
    std::uint64_t total = 0;
    std::uint64_t idle = 0;
    if (readLinuxCpu(total, idle)) {
        if (cpuBaselineReady_ && total > previousCpuTotal_) {
            const std::uint64_t deltaTotal = total - previousCpuTotal_;
            const std::uint64_t deltaIdle = idle - previousCpuIdle_;
            stats.cpuPercent = 100.0 * (1.0 - static_cast<double>(deltaIdle) / static_cast<double>(deltaTotal));
            stats.cpuAvailable = true;
        }
        previousCpuTotal_ = total;
        previousCpuIdle_ = idle;
        cpuBaselineReady_ = true;
    }
    readLinuxMemory(stats.memoryUsedBytes, stats.memoryTotalBytes);
#endif

    if (nvml_ != nullptr) {
        nvmlUtilization_t utilization{};
        nvmlMemory_t memory{};
        unsigned temperature = 0;
        if (nvml_->getUtilization(nvml_->device, &utilization) == NVML_SUCCESS &&
            nvml_->getMemory(nvml_->device, &memory) == NVML_SUCCESS &&
            nvml_->getTemperature(nvml_->device, NVML_TEMPERATURE_GPU, &temperature) == NVML_SUCCESS) {
            stats.gpuDetailsAvailable = true;
            stats.gpuUtilizationPercent = utilization.gpu;
            stats.gpuMemoryUsedBytes = memory.used;
            stats.gpuMemoryTotalBytes = memory.total;
            stats.gpuTemperatureCelsius = temperature;
        }
    }
    return stats;
}

std::string formatBytes(std::uint64_t bytes) {
    static constexpr const char* kUnits[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < std::size(kUnits)) {
        value /= 1024.0;
        ++unit;
    }
    std::ostringstream text;
    text << std::fixed << std::setprecision(unit == 0 ? 0 : 1) << value << ' ' << kUnits[unit];
    return text.str();
}

}  // namespace pie
