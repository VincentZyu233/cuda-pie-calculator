#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef PIE_EXPORTS
#ifdef _WIN32
#define PIE_API __declspec(dllexport)
#else
#define PIE_API __attribute__((visibility("default")))
#endif
#else
#define PIE_API
#endif

typedef struct PieGpuInfo {
    int32_t available;
    int32_t device_index;
    char name[256];
    char reason[256];
    uint64_t total_memory_bytes;
    int32_t compute_major;
    int32_t compute_minor;
    int32_t multiprocessor_count;
    int32_t core_clock_khz;
    int32_t memory_clock_khz;
    int32_t memory_bus_width_bits;
    int32_t driver_version;
    int32_t runtime_version;
} PieGpuInfo;

typedef struct PieJobSnapshot {
    int32_t state;
    int32_t mode;
    uint32_t requested_digits;
    uint32_t completed_steps;
    uint32_t total_steps;
    double gpu_milliseconds;
    uint64_t sample_target;
    uint64_t samples_completed;
    uint64_t hits_inside_circle;
    double monte_carlo_estimate;
    double monte_carlo_confidence95;
    double samples_per_second;
    char phase[256];
    char message[512];
} PieJobSnapshot;

typedef struct PieResourceStats {
    int32_t cpu_available;
    double cpu_percent;
    uint64_t memory_used_bytes;
    uint64_t memory_total_bytes;
    int32_t gpu_details_available;
    uint32_t gpu_utilization_percent;
    uint64_t gpu_memory_used_bytes;
    uint64_t gpu_memory_total_bytes;
    uint32_t gpu_temperature_celsius;
} PieResourceStats;

PIE_API void* pie_engine_create(void);
PIE_API void pie_engine_destroy(void* engine);
PIE_API int32_t pie_engine_device_count(const void* engine);
PIE_API int32_t pie_engine_selected_slot(const void* engine);
PIE_API int32_t pie_engine_select_device(void* engine, int32_t slot);
PIE_API void pie_engine_get_device(const void* engine, int32_t slot, PieGpuInfo* out);
PIE_API void pie_engine_get_device_snapshot(const void* engine, int32_t slot, PieJobSnapshot* out);
PIE_API void pie_engine_get_snapshot(const void* engine, PieJobSnapshot* out);
PIE_API size_t pie_engine_copy_result(const void* engine, char* buffer, size_t capacity);
PIE_API int32_t pie_engine_start_exact(void* engine, uint32_t digits);
PIE_API int32_t pie_engine_start_monte_carlo(void* engine, uint64_t samples);
PIE_API void pie_engine_toggle_pause(void* engine);
PIE_API void pie_engine_cancel(void* engine);
PIE_API void pie_engine_stop(void* engine);

PIE_API void* pie_resource_create(const PieGpuInfo* gpu);
PIE_API void pie_resource_destroy(void* resource);
PIE_API void pie_resource_sample(void* resource, PieResourceStats* out);

#ifdef __cplusplus
}
#endif
