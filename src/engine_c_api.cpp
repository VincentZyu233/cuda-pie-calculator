#include "engine_c_api.h"

#include "pi_engine.hpp"
#include "resources.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>

namespace {

template <std::size_t N>
void copyText(char (&destination)[N], const std::string& source) {
    std::snprintf(destination, N, "%s", source.c_str());
}

}  // namespace

extern "C" {

void* pie_engine_create(void) {
    try {
        return new pie::PiEngine();
    } catch (...) {
        return nullptr;
    }
}

void pie_engine_destroy(void* engine) {
    delete static_cast<pie::PiEngine*>(engine);
}

int32_t pie_engine_device_count(const void* engine) {
    if (engine == nullptr) {
        return 0;
    }
    return static_cast<int32_t>(static_cast<const pie::PiEngine*>(engine)->devices().size());
}

int32_t pie_engine_selected_slot(const void* engine) {
    if (engine == nullptr) {
        return 0;
    }
    return static_cast<int32_t>(static_cast<const pie::PiEngine*>(engine)->selectedDeviceSlot());
}

int32_t pie_engine_select_device(void* engine, int32_t slot) {
    if (engine == nullptr || slot < 0) {
        return 0;
    }
    return static_cast<pie::PiEngine*>(engine)->selectDevice(static_cast<std::size_t>(slot)) ? 1 : 0;
}

void pie_engine_get_device(const void* engine, int32_t slot, PieGpuInfo* out) {
    if (engine == nullptr || out == nullptr || slot < 0) {
        return;
    }
    std::memset(out, 0, sizeof(PieGpuInfo));

    const auto devices = static_cast<const pie::PiEngine*>(engine)->devices();
    const auto index = static_cast<std::size_t>(slot);
    if (index >= devices.size()) {
        return;
    }

    const pie::GpuInfo& gpu = devices[index].gpu;
    out->available = gpu.available ? 1 : 0;
    out->device_index = gpu.deviceIndex;
    copyText(out->name, gpu.name);
    copyText(out->reason, gpu.reason);
    out->total_memory_bytes = gpu.totalMemoryBytes;
    out->compute_major = gpu.computeMajor;
    out->compute_minor = gpu.computeMinor;
    out->multiprocessor_count = gpu.multiprocessorCount;
    out->core_clock_khz = gpu.coreClockKHz;
    out->memory_clock_khz = gpu.memoryClockKHz;
    out->memory_bus_width_bits = gpu.memoryBusWidthBits;
    out->driver_version = gpu.driverVersion;
    out->runtime_version = gpu.runtimeVersion;
}

void pie_engine_get_device_snapshot(const void* engine, int32_t slot, PieJobSnapshot* out) {
    if (engine == nullptr || out == nullptr || slot < 0) {
        return;
    }
    std::memset(out, 0, sizeof(PieJobSnapshot));

    const auto devices = static_cast<const pie::PiEngine*>(engine)->devices();
    const auto index = static_cast<std::size_t>(slot);
    if (index >= devices.size()) {
        return;
    }

    const auto& job = devices[index].job;
    out->state = static_cast<int32_t>(job.state);
    out->mode = static_cast<int32_t>(job.mode);
    out->requested_digits = job.requestedDigits;
    out->completed_steps = job.completedSteps;
    out->total_steps = job.totalSteps;
    out->gpu_milliseconds = job.gpuMilliseconds;
    out->sample_target = job.sampleTarget;
    out->samples_completed = job.samplesCompleted;
    out->hits_inside_circle = job.hitsInsideCircle;
    out->monte_carlo_estimate = job.monteCarloEstimate;
    out->monte_carlo_confidence95 = job.monteCarloConfidence95;
    out->samples_per_second = job.samplesPerSecond;
    copyText(out->phase, job.phase);
    copyText(out->message, job.message);
}

void pie_engine_get_snapshot(const void* engine, PieJobSnapshot* out) {
    if (engine == nullptr || out == nullptr) {
        return;
    }
    std::memset(out, 0, sizeof(PieJobSnapshot));

    const auto& job = static_cast<const pie::PiEngine*>(engine)->snapshot();
    out->state = static_cast<int32_t>(job.state);
    out->mode = static_cast<int32_t>(job.mode);
    out->requested_digits = job.requestedDigits;
    out->completed_steps = job.completedSteps;
    out->total_steps = job.totalSteps;
    out->gpu_milliseconds = job.gpuMilliseconds;
    out->sample_target = job.sampleTarget;
    out->samples_completed = job.samplesCompleted;
    out->hits_inside_circle = job.hitsInsideCircle;
    out->monte_carlo_estimate = job.monteCarloEstimate;
    out->monte_carlo_confidence95 = job.monteCarloConfidence95;
    out->samples_per_second = job.samplesPerSecond;
    copyText(out->phase, job.phase);
    copyText(out->message, job.message);
}

size_t pie_engine_copy_result(const void* engine, char* buffer, size_t capacity) {
    if (engine == nullptr) {
        return 0;
    }

    const std::string& result = static_cast<const pie::PiEngine*>(engine)->snapshot().result;
    if (buffer == nullptr || capacity == 0) {
        return result.size();
    }

    const std::size_t count = std::min(result.size(), capacity - 1);
    std::memcpy(buffer, result.data(), count);
    buffer[count] = '\0';
    return result.size();
}

int32_t pie_engine_start_exact(void* engine, uint32_t digits) {
    if (engine == nullptr) {
        return 0;
    }
    return static_cast<pie::PiEngine*>(engine)->startExact(digits) ? 1 : 0;
}

int32_t pie_engine_start_monte_carlo(void* engine, uint64_t samples) {
    if (engine == nullptr) {
        return 0;
    }
    return static_cast<pie::PiEngine*>(engine)->startMonteCarlo(samples) ? 1 : 0;
}

void pie_engine_toggle_pause(void* engine) {
    if (engine != nullptr) {
        static_cast<pie::PiEngine*>(engine)->togglePause();
    }
}

void pie_engine_cancel(void* engine) {
    if (engine != nullptr) {
        static_cast<pie::PiEngine*>(engine)->cancel();
    }
}

void pie_engine_stop(void* engine) {
    if (engine != nullptr) {
        static_cast<pie::PiEngine*>(engine)->stop();
    }
}

void* pie_resource_create(const PieGpuInfo* gpu) {
    try {
        pie::GpuInfo info;
        if (gpu != nullptr) {
            info.available = gpu->available != 0;
            info.deviceIndex = gpu->device_index;
            info.name = gpu->name;
            info.reason = gpu->reason;
            info.totalMemoryBytes = gpu->total_memory_bytes;
            info.computeMajor = gpu->compute_major;
            info.computeMinor = gpu->compute_minor;
            info.multiprocessorCount = gpu->multiprocessor_count;
            info.coreClockKHz = gpu->core_clock_khz;
            info.memoryClockKHz = gpu->memory_clock_khz;
            info.memoryBusWidthBits = gpu->memory_bus_width_bits;
            info.driverVersion = gpu->driver_version;
            info.runtimeVersion = gpu->runtime_version;
        }
        return new pie::ResourceMonitor(info);
    } catch (...) {
        return nullptr;
    }
}

void pie_resource_destroy(void* resource) {
    delete static_cast<pie::ResourceMonitor*>(resource);
}

void pie_resource_sample(void* resource, PieResourceStats* out) {
    if (resource == nullptr || out == nullptr) {
        return;
    }
    std::memset(out, 0, sizeof(PieResourceStats));

    const pie::ResourceStats stats = static_cast<pie::ResourceMonitor*>(resource)->sample();
    out->cpu_available = stats.cpuAvailable ? 1 : 0;
    out->cpu_percent = stats.cpuPercent;
    out->memory_used_bytes = stats.memoryUsedBytes;
    out->memory_total_bytes = stats.memoryTotalBytes;
    out->gpu_details_available = stats.gpuDetailsAvailable ? 1 : 0;
    out->gpu_utilization_percent = stats.gpuUtilizationPercent;
    out->gpu_memory_used_bytes = stats.gpuMemoryUsedBytes;
    out->gpu_memory_total_bytes = stats.gpuMemoryTotalBytes;
    out->gpu_temperature_celsius = stats.gpuTemperatureCelsius;
}

}  // extern "C"