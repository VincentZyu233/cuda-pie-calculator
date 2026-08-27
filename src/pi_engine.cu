#include "pi_engine.hpp"

#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace pie {
namespace {

constexpr std::uint32_t kBase = 10000;
constexpr unsigned kGuardDecimalDigits = 24;

class Cancelled final : public std::exception {
public:
    const char* what() const noexcept override { return "cancelled"; }
};

void checkCuda(cudaError_t error, const char* action) {
    if (error != cudaSuccess) {
        throw std::runtime_error(std::string(action) + ": " + cudaGetErrorString(error));
    }
}

class DeviceArray {
public:
    explicit DeviceArray(std::size_t count) : count_(count) {
        checkCuda(cudaMalloc(&data_, count_ * sizeof(std::uint32_t)), "CUDA memory allocation failed");
    }

    ~DeviceArray() {
        if (data_ != nullptr) {
            cudaFree(data_);
        }
    }

    DeviceArray(const DeviceArray&) = delete;
    DeviceArray& operator=(const DeviceArray&) = delete;

    std::uint32_t* data() const noexcept { return data_; }
    std::size_t count() const noexcept { return count_; }

private:
    std::uint32_t* data_ = nullptr;
    std::size_t count_ = 0;
};

__global__ void initializeAtanKernel(std::uint32_t* term, std::uint32_t* sum, std::size_t limbs, unsigned divisor) {
    if (blockIdx.x != 0 || threadIdx.x != 0) {
        return;
    }

    std::uint64_t remainder = 1;
    for (std::size_t i = 0; i < limbs; ++i) {
        const std::uint64_t dividend = remainder * kBase;
        const auto digit = static_cast<std::uint32_t>(dividend / divisor);
        term[i] = digit;
        sum[i] = digit;
        remainder = dividend % divisor;
    }
}

// One CUDA thread performs carry-sensitive fixed-point work. The data is never
// calculated on the host; the compact base-10000 representation is copied back
// only after the final GPU result is complete.
__global__ void atanStepKernel(
    std::uint32_t* term,
    std::uint32_t* sum,
    std::size_t limbs,
    unsigned iteration,
    unsigned qSquared,
    bool subtract) {
    if (blockIdx.x != 0 || threadIdx.x != 0) {
        return;
    }

    const std::uint64_t numerator = 2ULL * iteration + 1ULL;
    const std::uint64_t denominator = (2ULL * iteration + 3ULL) * qSquared;

    std::uint64_t carry = 0;
    for (std::size_t index = limbs; index-- > 0;) {
        const std::uint64_t product = static_cast<std::uint64_t>(term[index]) * numerator + carry;
        term[index] = static_cast<std::uint32_t>(product % kBase);
        carry = product / kBase;
    }

    std::uint64_t remainder = carry;
    for (std::size_t index = 0; index < limbs; ++index) {
        const std::uint64_t dividend = remainder * kBase + term[index];
        term[index] = static_cast<std::uint32_t>(dividend / denominator);
        remainder = dividend % denominator;
    }

    if (subtract) {
        unsigned borrow = 0;
        for (std::size_t index = limbs; index-- > 0;) {
            const std::uint64_t subtrahend = static_cast<std::uint64_t>(term[index]) + borrow;
            if (static_cast<std::uint64_t>(sum[index]) >= subtrahend) {
                sum[index] = static_cast<std::uint32_t>(static_cast<std::uint64_t>(sum[index]) - subtrahend);
                borrow = 0;
            } else {
                sum[index] = static_cast<std::uint32_t>(static_cast<std::uint64_t>(sum[index]) + kBase - subtrahend);
                borrow = 1;
            }
        }
    } else {
        carry = 0;
        for (std::size_t index = limbs; index-- > 0;) {
            const std::uint64_t value = static_cast<std::uint64_t>(sum[index]) + term[index] + carry;
            sum[index] = static_cast<std::uint32_t>(value % kBase);
            carry = value / kBase;
        }
    }
}

__global__ void machInQuarterKernel(
    const std::uint32_t* atanOneFifth,
    const std::uint32_t* atanOne239,
    std::uint32_t* quarterPi,
    std::size_t limbs) {
    if (blockIdx.x != 0 || threadIdx.x != 0) {
        return;
    }

    std::uint64_t carry = 0;
    for (std::size_t index = limbs; index-- > 0;) {
        const std::uint64_t product = static_cast<std::uint64_t>(atanOneFifth[index]) * 4ULL + carry;
        quarterPi[index] = static_cast<std::uint32_t>(product % kBase);
        carry = product / kBase;
    }

    unsigned borrow = 0;
    for (std::size_t index = limbs; index-- > 0;) {
        const std::uint64_t subtrahend = static_cast<std::uint64_t>(atanOne239[index]) + borrow;
        if (static_cast<std::uint64_t>(quarterPi[index]) >= subtrahend) {
            quarterPi[index] = static_cast<std::uint32_t>(static_cast<std::uint64_t>(quarterPi[index]) - subtrahend);
            borrow = 0;
        } else {
            quarterPi[index] = static_cast<std::uint32_t>(static_cast<std::uint64_t>(quarterPi[index]) + kBase - subtrahend);
            borrow = 1;
        }
    }
}

__global__ void scalePiKernel(std::uint32_t* quarterPi, std::size_t limbs, unsigned* integerPart) {
    if (blockIdx.x != 0 || threadIdx.x != 0) {
        return;
    }

    std::uint64_t carry = 0;
    for (std::size_t index = limbs; index-- > 0;) {
        const std::uint64_t product = static_cast<std::uint64_t>(quarterPi[index]) * 4ULL + carry;
        quarterPi[index] = static_cast<std::uint32_t>(product % kBase);
        carry = product / kBase;
    }
    *integerPart = static_cast<unsigned>(carry);
}

unsigned numberOfTerms(unsigned q, unsigned decimalDigits) {
    const double denominator = 2.0 * std::log10(static_cast<double>(q));
    return static_cast<unsigned>(std::ceil(static_cast<double>(decimalDigits) / denominator)) + 3U;
}

std::string formatDigits(unsigned integerPart, const std::vector<std::uint32_t>& limbs, unsigned decimalDigits) {
    std::string output = std::to_string(integerPart);
    output.push_back('.');
    output.reserve(output.size() + limbs.size() * 4U);

    for (const auto limb : limbs) {
        char group[5]{};
        std::snprintf(group, sizeof(group), "%04u", limb);
        output.append(group);
    }

    output.resize(std::min(output.size(), std::to_string(integerPart).size() + 1U + decimalDigits));
    return output;
}

GpuInfo probeGpu() {
    GpuInfo info;
    int deviceCount = 0;
    const cudaError_t countResult = cudaGetDeviceCount(&deviceCount);
    if (countResult != cudaSuccess || deviceCount <= 0) {
        const char* detail = countResult == cudaSuccess ? "no CUDA devices were returned" : cudaGetErrorString(countResult);
        info.reason = std::string("GPU unavailable: ") + detail;
        cudaGetLastError();
        return info;
    }

    cudaDeviceProp properties{};
    const cudaError_t propertiesResult = cudaGetDeviceProperties(&properties, 0);
    if (propertiesResult != cudaSuccess) {
        info.reason = std::string("GPU unavailable: ") + cudaGetErrorString(propertiesResult);
        cudaGetLastError();
        return info;
    }

    int driver = 0;
    int runtime = 0;
    cudaDriverGetVersion(&driver);
    cudaRuntimeGetVersion(&runtime);

    info.available = true;
    info.deviceIndex = 0;
    info.name = properties.name;
    info.totalMemoryBytes = properties.totalGlobalMem;
    info.computeMajor = properties.major;
    info.computeMinor = properties.minor;
    info.driverVersion = driver;
    info.runtimeVersion = runtime;
    return info;
}

}  // namespace

const char* stateLabel(JobState state) {
    switch (state) {
        case JobState::GpuUnavailable: return "GPU unavailable";
        case JobState::Idle: return "Ready";
        case JobState::Preparing: return "Preparing CUDA job";
        case JobState::Running: return "Calculating on GPU";
        case JobState::Paused: return "Paused";
        case JobState::Cancelling: return "Stopping";
        case JobState::Finished: return "Finished";
        case JobState::Cancelled: return "Cancelled";
        case JobState::Failed: return "Failed";
    }
    return "Unknown";
}

PiEngine::PiEngine() : gpu_(probeGpu()) {
    snapshot_.state = gpu_.available ? JobState::Idle : JobState::GpuUnavailable;
    snapshot_.message = gpu_.available ? "CUDA device ready. Press s to start." : gpu_.reason;
}

PiEngine::~PiEngine() {
    stop();
}

const GpuInfo& PiEngine::gpu() const noexcept {
    return gpu_;
}

JobSnapshot PiEngine::snapshot() const {
    std::scoped_lock lock(snapshotMutex_);
    return snapshot_;
}

void PiEngine::setSnapshot(const JobSnapshot& value) {
    std::scoped_lock lock(snapshotMutex_);
    snapshot_ = value;
}

void PiEngine::updateProgress(unsigned complete, unsigned total, const std::string& phase) {
    std::scoped_lock lock(snapshotMutex_);
    snapshot_.completedSteps = complete;
    snapshot_.totalSteps = total;
    snapshot_.phase = phase;
    if (snapshot_.state == JobState::Preparing) {
        snapshot_.state = JobState::Running;
    }
}

bool PiEngine::start(unsigned digits) {
    if (!gpu_.available) {
        std::scoped_lock lock(snapshotMutex_);
        snapshot_.state = JobState::GpuUnavailable;
        snapshot_.message = gpu_.reason;
        return false;
    }
    if (digits < kMinimumDigits || digits > kMaximumDigits) {
        std::scoped_lock lock(snapshotMutex_);
        snapshot_.state = JobState::Failed;
        snapshot_.message = "Requested precision is outside the supported 10 to 10000 digit range.";
        return false;
    }

    if (worker_.joinable()) {
        const JobState current = snapshot().state;
        if (current == JobState::Preparing || current == JobState::Running || current == JobState::Paused || current == JobState::Cancelling) {
            return false;
        }
        worker_.join();
    }

    cancelRequested_.store(false);
    paused_.store(false);
    JobSnapshot initial;
    initial.state = JobState::Preparing;
    initial.requestedDigits = digits;
    initial.message = "Allocating CUDA fixed-point buffers.";
    setSnapshot(initial);
    worker_ = std::thread(&PiEngine::run, this, digits);
    return true;
}

void PiEngine::togglePause() {
    const JobState current = snapshot().state;
    if (current == JobState::Running) {
        paused_.store(true);
        std::scoped_lock lock(snapshotMutex_);
        snapshot_.state = JobState::Paused;
        snapshot_.message = "CUDA work will pause after the current kernel.";
    } else if (current == JobState::Paused) {
        paused_.store(false);
        {
            std::scoped_lock lock(snapshotMutex_);
            snapshot_.state = JobState::Running;
            snapshot_.message = "CUDA calculation resumed.";
        }
        pauseChanged_.notify_all();
    }
}

void PiEngine::cancel() {
    cancelRequested_.store(true);
    paused_.store(false);
    {
        std::scoped_lock lock(snapshotMutex_);
        if (snapshot_.state == JobState::Preparing || snapshot_.state == JobState::Running || snapshot_.state == JobState::Paused) {
            snapshot_.state = JobState::Cancelling;
            snapshot_.message = "Cancelling CUDA job.";
        }
    }
    pauseChanged_.notify_all();
}

void PiEngine::stop() {
    cancel();
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool PiEngine::cancellationRequested() const noexcept {
    return cancelRequested_.load();
}

void PiEngine::waitWhilePaused() {
    std::unique_lock lock(pauseMutex_);
    pauseChanged_.wait(lock, [this] { return !paused_.load() || cancellationRequested(); });
}

void PiEngine::run(unsigned digits) {
    cudaStream_t stream = nullptr;
    cudaEvent_t started = nullptr;
    cudaEvent_t finished = nullptr;

    try {
        checkCuda(cudaSetDevice(gpu_.deviceIndex), "CUDA device selection failed");
        checkCuda(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking), "CUDA stream creation failed");
        checkCuda(cudaEventCreate(&started), "CUDA start event creation failed");
        checkCuda(cudaEventCreate(&finished), "CUDA finish event creation failed");

        const unsigned workingDigits = digits + kGuardDecimalDigits;
        const std::size_t limbs = (workingDigits + 3U) / 4U;
        const unsigned fifthTerms = numberOfTerms(5U, workingDigits);
        const unsigned twoThirtyNineTerms = numberOfTerms(239U, workingDigits);
        const unsigned totalSteps = fifthTerms + twoThirtyNineTerms + 3U;
        unsigned complete = 0;

        DeviceArray atanFifth(limbs);
        DeviceArray atanTwoThirtyNine(limbs);
        DeviceArray term(limbs);
        DeviceArray quarterPi(limbs);
        DeviceArray integerPart(1);

        checkCuda(cudaEventRecord(started, stream), "CUDA start event recording failed");

        const auto calculateAtan = [&](unsigned q, DeviceArray& destination, const char* phase) {
            initializeAtanKernel<<<1, 1, 0, stream>>>(term.data(), destination.data(), limbs, q);
            checkCuda(cudaGetLastError(), "CUDA arctangent initialization failed");
            checkCuda(cudaStreamSynchronize(stream), "CUDA arctangent initialization synchronization failed");
            ++complete;
            updateProgress(complete, totalSteps, phase);

            const unsigned qSquared = q * q;
            const unsigned termCount = q == 5U ? fifthTerms : twoThirtyNineTerms;
            for (unsigned iteration = 0; iteration + 1U < termCount; ++iteration) {
                waitWhilePaused();
                if (cancellationRequested()) {
                    throw Cancelled();
                }

                atanStepKernel<<<1, 1, 0, stream>>>(
                    term.data(), destination.data(), limbs, iteration, qSquared, (iteration % 2U) == 0U);
                checkCuda(cudaGetLastError(), "CUDA arctangent step failed");

                if ((iteration + 1U) % 8U == 0U || iteration + 2U == termCount) {
                    checkCuda(cudaStreamSynchronize(stream), "CUDA arctangent synchronization failed");
                }
                ++complete;
                if ((iteration + 1U) % 4U == 0U || iteration + 2U == termCount) {
                    updateProgress(complete, totalSteps, phase);
                }
            }
        };

        calculateAtan(5U, atanFifth, "Computing atan(1/5) on CUDA GPU");
        calculateAtan(239U, atanTwoThirtyNine, "Computing atan(1/239) on CUDA GPU");

        waitWhilePaused();
        if (cancellationRequested()) {
            throw Cancelled();
        }
        machInQuarterKernel<<<1, 1, 0, stream>>>(atanFifth.data(), atanTwoThirtyNine.data(), quarterPi.data(), limbs);
        checkCuda(cudaGetLastError(), "CUDA Machin combination failed");
        checkCuda(cudaStreamSynchronize(stream), "CUDA Machin combination synchronization failed");
        ++complete;
        updateProgress(complete, totalSteps, "Combining Machin formula on CUDA GPU");

        scalePiKernel<<<1, 1, 0, stream>>>(quarterPi.data(), limbs, integerPart.data());
        checkCuda(cudaGetLastError(), "CUDA Pi scaling failed");
        checkCuda(cudaStreamSynchronize(stream), "CUDA Pi scaling synchronization failed");
        ++complete;
        updateProgress(complete, totalSteps, "Formatting CUDA fixed-point result");

        std::vector<std::uint32_t> hostLimbs(limbs);
        unsigned hostInteger = 0;
        checkCuda(cudaMemcpyAsync(hostLimbs.data(), quarterPi.data(), limbs * sizeof(std::uint32_t), cudaMemcpyDeviceToHost, stream),
                  "CUDA result copy failed");
        checkCuda(cudaMemcpyAsync(&hostInteger, integerPart.data(), sizeof(hostInteger), cudaMemcpyDeviceToHost, stream),
                  "CUDA integer result copy failed");
        checkCuda(cudaEventRecord(finished, stream), "CUDA finish event recording failed");
        checkCuda(cudaStreamSynchronize(stream), "CUDA final synchronization failed");
        float elapsedMilliseconds = 0.0F;
        checkCuda(cudaEventElapsedTime(&elapsedMilliseconds, started, finished), "CUDA elapsed time query failed");
        ++complete;

        JobSnapshot result;
        result.state = JobState::Finished;
        result.requestedDigits = digits;
        result.completedSteps = complete;
        result.totalSteps = totalSteps;
        result.gpuMilliseconds = elapsedMilliseconds;
        result.phase = "CUDA calculation complete";
        result.message = "Result was calculated on the CUDA GPU. CPU fallback is disabled.";
        result.result = formatDigits(hostInteger, hostLimbs, digits);
        setSnapshot(result);
    } catch (const Cancelled&) {
        JobSnapshot cancelled = snapshot();
        cancelled.state = JobState::Cancelled;
        cancelled.message = "CUDA calculation cancelled. No CPU fallback was used.";
        setSnapshot(cancelled);
    } catch (const std::exception& error) {
        JobSnapshot failed = snapshot();
        failed.state = JobState::Failed;
        failed.message = std::string("CUDA calculation failed: ") + error.what() + ". CPU fallback is disabled.";
        setSnapshot(failed);
    }

    if (finished != nullptr) {
        cudaEventDestroy(finished);
    }
    if (started != nullptr) {
        cudaEventDestroy(started);
    }
    if (stream != nullptr) {
        cudaStreamDestroy(stream);
    }
}

}  // namespace pie
