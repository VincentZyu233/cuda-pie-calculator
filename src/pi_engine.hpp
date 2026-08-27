#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace pie {

inline constexpr unsigned kMinimumDigits = 10;
inline constexpr unsigned kMaximumDigits = 10000;
inline constexpr std::uint64_t kMinimumMonteCarloSamples = 1'000'000ULL;
inline constexpr std::uint64_t kMaximumMonteCarloSamples = 4'000'000'000ULL;

enum class CalculationMode {
    ExactDigits,
    MonteCarlo,
};

enum class JobState {
    GpuUnavailable,
    Idle,
    Preparing,
    Running,
    Paused,
    Cancelling,
    Finished,
    Cancelled,
    Failed,
};

struct GpuInfo {
    bool available = false;
    int deviceIndex = -1;
    std::string name;
    std::string reason;
    std::uint64_t totalMemoryBytes = 0;
    int computeMajor = 0;
    int computeMinor = 0;
    int driverVersion = 0;
    int runtimeVersion = 0;
};

struct JobSnapshot {
    JobState state = JobState::GpuUnavailable;
    CalculationMode mode = CalculationMode::ExactDigits;
    unsigned requestedDigits = 1000;
    unsigned completedSteps = 0;
    unsigned totalSteps = 0;
    double gpuMilliseconds = 0.0;
    std::uint64_t sampleTarget = 0;
    std::uint64_t samplesCompleted = 0;
    std::uint64_t hitsInsideCircle = 0;
    double monteCarloEstimate = 0.0;
    double monteCarloConfidence95 = 0.0;
    double samplesPerSecond = 0.0;
    std::string phase;
    std::string message;
    std::string result;
};

const char* stateLabel(JobState state);
const char* modeLabel(CalculationMode mode);

class PiEngine {
public:
    PiEngine();
    ~PiEngine();

    PiEngine(const PiEngine&) = delete;
    PiEngine& operator=(const PiEngine&) = delete;

    const GpuInfo& gpu() const noexcept;
    JobSnapshot snapshot() const;
    bool startExact(unsigned digits);
    bool startMonteCarlo(std::uint64_t samples);
    void togglePause();
    void cancel();
    void stop();

private:
    bool beginJob(const JobSnapshot& initial);
    void runExact(unsigned digits);
    void runMonteCarlo(std::uint64_t samples);
    void setSnapshot(const JobSnapshot& value);
    void updateProgress(unsigned complete, unsigned total, const std::string& phase);
    void updateMonteCarloProgress(
        std::uint64_t completed,
        std::uint64_t target,
        std::uint64_t hits,
        double estimate,
        double confidence95,
        double samplesPerSecond);
    void waitWhilePaused();
    bool cancellationRequested() const noexcept;

    GpuInfo gpu_;
    mutable std::mutex snapshotMutex_;
    JobSnapshot snapshot_;
    std::thread worker_;
    std::atomic<bool> cancelRequested_{false};
    std::atomic<bool> paused_{false};
    std::mutex pauseMutex_;
    std::condition_variable pauseChanged_;
};

}  // namespace pie
