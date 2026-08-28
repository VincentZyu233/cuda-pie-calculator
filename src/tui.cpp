#include "tui.hpp"

#include "resources.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace pie {
namespace {

constexpr std::size_t kHistorySize = 60;
constexpr std::size_t kResultWrapWidth = 80;

bool jobIsActive(JobState state) {
    return state == JobState::Preparing || state == JobState::Running ||
           state == JobState::Paused || state == JobState::Cancelling;
}

struct UiState {
    std::vector<DeviceSnapshot> devices;
    std::size_t selectedDeviceSlot = 0;
    JobSnapshot job;
    GpuInfo gpu;
    ResourceStats resource;

    CalculationMode selectedMode = CalculationMode::ExactDigits;
    unsigned selectedDigits = kDefaultDigits;
    std::uint64_t selectedSamples = 100'000'000ULL;

    std::size_t scrollOffset = 0;
    bool showHelp = false;

    std::vector<double> progressHistory;
    std::vector<double> throughputHistory;
    std::vector<double> gpuHistory;
    std::vector<std::string> resultLines;
};

std::vector<std::string> wrapText(const std::string& text, std::size_t maxWidth) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string rawLine;
    while (std::getline(stream, rawLine)) {
        if (rawLine.empty()) {
            lines.emplace_back();
            continue;
        }
        std::string current;
        std::size_t currentWidth = 0;
        std::size_t i = 0;
        while (i < rawLine.size()) {
            const unsigned char c = static_cast<unsigned char>(rawLine[i]);
            std::size_t charLen = 1;
            int charWidth = 1;
            if ((c & 0xE0) == 0xC0) {
                charLen = 2;
                charWidth = 2;
            } else if ((c & 0xF0) == 0xE0) {
                charLen = 3;
                charWidth = 2;
            } else if ((c & 0xF8) == 0xF0) {
                charLen = 4;
                charWidth = 2;
            }
            if (i + charLen > rawLine.size()) {
                charLen = rawLine.size() - i;
            }
            const std::string glyph = rawLine.substr(i, charLen);
            if (charWidth + static_cast<int>(currentWidth) > static_cast<int>(maxWidth) &&
                !current.empty()) {
                lines.push_back(current);
                current.clear();
                currentWidth = 0;
            }
            current += glyph;
            currentWidth += static_cast<std::size_t>(charWidth);
            i += charLen;
        }
        lines.push_back(current);
    }
    return lines;
}

std::string percentString(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << value << "%";
    return out.str();
}

std::string memUsage(const ResourceStats& stats) {
    if (stats.memoryTotalBytes == 0) {
        return "未知";
    }
    return formatBytes(stats.memoryUsedBytes) + " / " + formatBytes(stats.memoryTotalBytes);
}

Color stateColor(JobState state) {
    switch (state) {
        case JobState::Running:
            return Color::Green;
        case JobState::Paused:
            return Color::Yellow;
        case JobState::Finished:
            return Color::Cyan;
        case JobState::Failed:
        case JobState::GpuUnavailable:
            return Color::Red;
        case JobState::Cancelling:
            return Color::Magenta;
        case JobState::Idle:
            return Color::Green;
        case JobState::Preparing:
            return Color::Yellow;
        case JobState::Cancelled:
            return Color::GrayDark;
    }
    return Color::White;
}

Element sparkline(const std::vector<double>& history, double maxValue, Color colorChoice) {
    GraphFunction fn = [history, maxValue](int width, int height) -> std::vector<int> {
        if (width <= 0 || height <= 0) {
            return {};
        }
        std::vector<int> data(static_cast<std::size_t>(width), height - 1);
        if (history.empty()) {
            return data;
        }
        double peak = maxValue;
        if (peak <= 0.0) {
            peak = 1.0;
            for (double value : history) {
                peak = std::max(peak, value);
            }
        }
        for (int x = 0; x < width; ++x) {
            const double position =
                static_cast<double>(x) / static_cast<double>(width) * static_cast<double>(history.size());
            std::size_t index = static_cast<std::size_t>(position);
            if (index >= history.size()) {
                index = history.size() - 1;
            }
            const double value = history[index];
            int y = height - 1;
            if (value > 0.0 && peak > 0.0) {
                const double ratio = std::min(1.0, value / peak);
                y = std::max(0, height - 1 - static_cast<int>(std::lround(ratio * (height - 1))));
            }
            data[static_cast<std::size_t>(x)] = y;
        }
        return data;
    };
    return color(colorChoice, graph(fn));
}

Element buildHeader(const UiState& view) {
    std::string modeText = std::string("模式: ") + modeLabel(view.selectedMode);
    std::string stateText = std::string("状态: ") + stateLabel(view.job.state);
    std::string valueText;
    if (view.selectedMode == CalculationMode::ExactDigits) {
        valueText = std::string("位数: ") + std::to_string(view.selectedDigits);
    } else {
        valueText = std::string("样本: ") + std::to_string(view.selectedSamples);
    }
    const std::string gpuName = view.gpu.name.empty() ? "未知 GPU" : view.gpu.name;

    Element left = color(Color::Cyan, bold(text("  π CUDA 计算器  ")));
    Element right = hbox({
        dim(text(modeText + "  ")),
        bold(color(stateColor(view.job.state), text(stateText + "  "))),
        dim(text(valueText + "  ")),
        color(Color::Magenta, text(gpuName)),
    });
    return hbox({left, filler(), right});
}

Element buildDevicePanel(const UiState& view) {
    std::vector<Element> rows;
    rows.push_back(bold(color(Color::Cyan, text("  设备  "))));

    if (view.devices.empty()) {
        rows.push_back(dim(text("  未发现 CUDA 设备")));
    }
    for (std::size_t i = 0; i < view.devices.size(); ++i) {
        const DeviceSnapshot& device = view.devices[i];
        const bool selected = (i == view.selectedDeviceSlot);
        const std::string label = (selected ? "▶ " : "  ") + device.gpu.name + "  " +
                                  stateLabel(device.job.state);
        if (selected) {
            rows.push_back(color(Color::Green, bold(text(label))));
        } else {
            rows.push_back(dim(text(label)));
        }
    }

    rows.push_back(separator());
    rows.push_back(bold(color(Color::Cyan, text("  GPU 信息  "))));
    if (view.gpu.available) {
        rows.push_back(text("  名称  " + view.gpu.name));
        rows.push_back(text(
            "  算力  " + std::to_string(view.gpu.computeMajor) + "." + std::to_string(view.gpu.computeMinor)));
        rows.push_back(text("  SM    " + std::to_string(view.gpu.multiprocessorCount)));
        rows.push_back(text("  显存  " + formatBytes(view.gpu.totalMemoryBytes)));
        if (view.resource.gpuDetailsAvailable) {
            rows.push_back(text(
                "  利用率 " + percentString(static_cast<double>(view.resource.gpuUtilizationPercent))));
            rows.push_back(text(
                "  温度  " + std::to_string(view.resource.gpuTemperatureCelsius) + "°C"));
            rows.push_back(text(
                "  显存用 " + formatBytes(view.resource.gpuMemoryUsedBytes) + " / " +
                formatBytes(view.resource.gpuMemoryTotalBytes)));
        }
    } else {
        rows.push_back(color(Color::Red, text("  " + view.gpu.reason)));
    }

    rows.push_back(separator());
    rows.push_back(bold(color(Color::Cyan, text("  系统资源  "))));
    rows.push_back(text("  CPU   " + percentString(view.resource.cpuPercent)));
    rows.push_back(text("  内存   " + memUsage(view.resource)));

    return window(text("设备 / 资源"), vbox(std::move(rows)));
}

Element buildResultBody(const UiState& view, int termHeight) {
    if (view.resultLines.empty()) {
        return color(Color::GrayDark, dim(text("  尚未开始计算，按空格键开始。")));
    }
    const int maxHeight = std::max(2, termHeight - 18);
    const std::size_t total = view.resultLines.size();
    const std::size_t maxStart =
        total > static_cast<std::size_t>(maxHeight) ? total - static_cast<std::size_t>(maxHeight) : 0;
    const std::size_t start = std::min(view.scrollOffset, maxStart);
    const std::size_t count = std::min<std::size_t>(total - start, static_cast<std::size_t>(maxHeight));
    std::vector<Element> lines;
    for (std::size_t i = 0; i < count; ++i) {
        const std::string& line = view.resultLines[start + i];
        lines.push_back(dim(text(line)));
    }
    return vbox(std::move(lines));
}

Element buildResultPanel(const UiState& view, int termWidth, int termHeight) {
    (void)termWidth;
    std::vector<Element> rows;

    rows.push_back(hbox({
        bold(color(Color::Cyan, text("  计算结果  "))),
        filler(),
        bold(color(stateColor(view.job.state), text(stateLabel(view.job.state)))),
    }));

    const std::string message =
        view.job.message.empty() ? stateLabel(view.job.state) : view.job.message;
    rows.push_back(color(Color::GrayDark, dim(text("  " + message))));

    double progress = 0.0;
    if (view.job.state == JobState::Finished) {
        progress = 1.0;
    } else if (view.job.totalSteps > 0) {
        progress = std::clamp(
            static_cast<double>(view.job.completedSteps) /
                static_cast<double>(view.job.totalSteps),
            0.0,
            1.0);
    }
    std::ostringstream progressText;
    progressText << std::fixed << std::setprecision(1) << (progress * 100.0) << "%";
    rows.push_back(hbox({
        text("  进度 "),
        filler(),
        bold(color(Color::Green, text(progressText.str()))),
    }));
    rows.push_back(color(Color::Green, gauge(progress)));

    rows.push_back(separator());
    rows.push_back(bold(color(Color::Cyan, text("  实时图  "))));
    rows.push_back(text("  计算进度  "));
    rows.push_back(sparkline(view.progressHistory, 1.0, Color::Green));
    if (view.job.mode == CalculationMode::MonteCarlo) {
        std::ostringstream throughputText;
        throughputText << "  吞吐量 " << std::fixed << std::setprecision(1)
                       << view.job.samplesPerSecond << " 样本/秒";
        rows.push_back(color(Color::Yellow, text(throughputText.str())));
        rows.push_back(sparkline(view.throughputHistory, 0.0, Color::Yellow));
    }
    if (view.resource.gpuDetailsAvailable) {
        std::ostringstream gpuText;
        gpuText << "  GPU 利用率 " << view.resource.gpuUtilizationPercent << "%";
        rows.push_back(color(Color::Magenta, text(gpuText.str())));
        rows.push_back(sparkline(view.gpuHistory, 100.0, Color::Magenta));
    }

    rows.push_back(separator());
    rows.push_back(dim(text("  输出（j/k 或 PgUp/PgDn 滚动）")));
    rows.push_back(buildResultBody(view, termHeight));

    return window(text("计算"), vbox(std::move(rows)));
}

Element buildHelpPanel() {
    std::vector<Element> lines = {
        bold(text("  快捷键  ")),
        separator(),
        text("  Space   开始 / 暂停 / 继续"),
        text("  c       取消当前计算"),
        text("  Tab     切换到下一个 GPU（空闲时）"),
        text("  e       选择精确位数模式"),
        text("  m       选择蒙特卡洛模式"),
        text("  + / =   增加位数或样本数"),
        text("  -       减少位数或样本数"),
        text("  PgUp / PgDn 或 j / k  滚动结果"),
        text("  h / ?   显示 / 隐藏帮助"),
        text("  q       退出"),
    };
    return window(text("帮助"), vbox(std::move(lines)));
}

Element buildHotkeyBar() {
    std::vector<Element> parts = {
        text(" Space:开始/暂停 "),
        text(" c:取消 "),
        text(" Tab:切换GPU "),
        text(" e/m:模式 "),
        text(" +/-:调整 "),
        text(" j/k:滚动 "),
        text(" h:帮助 "),
        text(" q:退出 "),
    };
    return color(Color::GrayDark, hbox(std::move(parts)));
}

Element buildDashboard(const UiState& view, int termWidth, int termHeight) {
    const int leftWidth = std::max(std::min(termWidth / 3, 38), 26);
    Element content;
    if (view.showHelp) {
        content = center(buildHelpPanel());
    } else {
        content = hbox({
            buildDevicePanel(view) | size(WIDTH, EQUAL, leftWidth),
            separator(),
            vbox({buildResultPanel(view, termWidth, termHeight)}) | flex,
        });
    }
    return vbox({
        buildHeader(view) | size(HEIGHT, EQUAL, 1),
        separator(),
        content | flex,
        separator(),
        buildHotkeyBar() | size(HEIGHT, EQUAL, 1),
    });
}

}  // namespace

int TerminalUi::run(PiEngine& engine) {
    auto screen = ScreenInteractive::Fullscreen();
    std::mutex mutex;
    UiState state;
    std::atomic<bool> keepRunning{true};

    auto snapshotUi = [&]() -> UiState {
        std::lock_guard<std::mutex> lock(mutex);
        return state;
    };

    auto pollThread = std::thread([&]() {
        auto monitor = std::make_unique<ResourceMonitor>(engine.selectedGpu());
        std::size_t monitoredSlot = engine.selectedDeviceSlot();
        while (keepRunning.load()) {
            const std::size_t slot = engine.selectedDeviceSlot();
            if (slot != monitoredSlot) {
                monitor = std::make_unique<ResourceMonitor>(engine.selectedGpu());
                monitoredSlot = slot;
            }

            JobSnapshot job = engine.snapshot();
            std::vector<DeviceSnapshot> devices = engine.devices();
            GpuInfo gpu = engine.selectedGpu();
            ResourceStats resource = monitor->sample();

            {
                std::lock_guard<std::mutex> lock(mutex);
                state.devices = std::move(devices);
                state.selectedDeviceSlot = slot;
                state.job = std::move(job);
                state.gpu = std::move(gpu);
                state.resource = resource;
                state.resultLines = wrapText(state.job.result, kResultWrapWidth);

                double progress = 0.0;
                if (state.job.state == JobState::Finished) {
                    progress = 1.0;
                } else if (state.job.totalSteps > 0) {
                    progress = std::clamp(
                        static_cast<double>(state.job.completedSteps) /
                            static_cast<double>(state.job.totalSteps),
                        0.0,
                        1.0);
                }
                state.progressHistory.push_back(progress);
                if (state.progressHistory.size() > kHistorySize) {
                    state.progressHistory.erase(state.progressHistory.begin());
                }

                state.throughputHistory.push_back(state.job.samplesPerSecond);
                if (state.throughputHistory.size() > kHistorySize) {
                    state.throughputHistory.erase(state.throughputHistory.begin());
                }

                state.gpuHistory.push_back(static_cast<double>(resource.gpuUtilizationPercent));
                if (state.gpuHistory.size() > kHistorySize) {
                    state.gpuHistory.erase(state.gpuHistory.begin());
                }
            }

            screen.PostEvent(Event::Custom);
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
    });

    auto runQuit = screen.ExitLoopClosure();
    auto component = Renderer([&]() -> Element {
        const UiState view = snapshotUi();
        return buildDashboard(view, screen.dimx(), screen.dimy());
    });

    auto handler = CatchEvent(component, [&](Event event) -> bool {
        if (event == Event::Custom) {
            return true;
        }

        if (event == Event::Character('q') || event == Event::Character('Q') ||
            event == Event::Escape) {
            keepRunning = false;
            engine.stop();
            runQuit();
            return true;
        }

        if (event == Event::Character('h') || event == Event::Character('H') ||
            event == Event::Character('?')) {
            std::lock_guard<std::mutex> lock(mutex);
            state.showHelp = !state.showHelp;
            return true;
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            if (state.showHelp) {
                return false;
            }
        }

        if (event == Event::Tab) {
            std::lock_guard<std::mutex> lock(mutex);
            if (!jobIsActive(state.job.state) && state.devices.size() > 1) {
                const std::size_t next = (state.selectedDeviceSlot + 1) % state.devices.size();
                if (engine.selectDevice(next)) {
                    state.scrollOffset = 0;
                }
            }
            return true;
        }
        if (event == Event::TabReverse) {
            std::lock_guard<std::mutex> lock(mutex);
            if (!jobIsActive(state.job.state) && state.devices.size() > 1) {
                const std::size_t next =
                    state.selectedDeviceSlot == 0
                        ? state.devices.size() - 1
                        : state.selectedDeviceSlot - 1;
                if (engine.selectDevice(next)) {
                    state.scrollOffset = 0;
                }
            }
            return true;
        }

        if (event == Event::Character(' ')) {
            std::lock_guard<std::mutex> lock(mutex);
            switch (state.job.state) {
                case JobState::Idle:
                case JobState::Finished:
                case JobState::Cancelled:
                case JobState::Failed:
                    if (state.gpu.available) {
                        if (state.selectedMode == CalculationMode::MonteCarlo) {
                            engine.startMonteCarlo(state.selectedSamples);
                        } else {
                            engine.startExact(state.selectedDigits);
                        }
                        state.scrollOffset = 0;
                    }
                    break;
                case JobState::Running:
                case JobState::Paused:
                    engine.togglePause();
                    break;
                default:
                    break;
            }
            return true;
        }

        if (event == Event::Character('c') || event == Event::Character('C')) {
            std::lock_guard<std::mutex> lock(mutex);
            if (jobIsActive(state.job.state)) {
                engine.cancel();
            }
            return true;
        }

        if (event == Event::Character('e') || event == Event::Character('E')) {
            std::lock_guard<std::mutex> lock(mutex);
            if (!jobIsActive(state.job.state)) {
                state.selectedMode = CalculationMode::ExactDigits;
                state.scrollOffset = 0;
            }
            return true;
        }
        if (event == Event::Character('m') || event == Event::Character('M')) {
            std::lock_guard<std::mutex> lock(mutex);
            if (!jobIsActive(state.job.state)) {
                state.selectedMode = CalculationMode::MonteCarlo;
                state.scrollOffset = 0;
            }
            return true;
        }

        if (event == Event::Character('+') || event == Event::Character('=')) {
            std::lock_guard<std::mutex> lock(mutex);
            if (!jobIsActive(state.job.state)) {
                if (state.selectedMode == CalculationMode::ExactDigits) {
                    state.selectedDigits =
                        std::min(kMaximumDigits, state.selectedDigits + 100U);
                } else {
                    state.selectedSamples = std::min<std::uint64_t>(
                        kMaximumMonteCarloSamples,
                        state.selectedSamples + 10'000'000ULL);
                }
            }
            return true;
        }
        if (event == Event::Character('-')) {
            std::lock_guard<std::mutex> lock(mutex);
            if (!jobIsActive(state.job.state)) {
                if (state.selectedMode == CalculationMode::ExactDigits) {
                    state.selectedDigits =
                        std::max(kMinimumDigits,
                                 state.selectedDigits > 100U
                                     ? state.selectedDigits - 100U
                                     : kMinimumDigits);
                } else {
                    state.selectedSamples =
                        state.selectedSamples > kMinimumMonteCarloSamples + 10'000'000ULL
                            ? state.selectedSamples - 10'000'000ULL
                            : kMinimumMonteCarloSamples;
                }
            }
            return true;
        }

        if (event == Event::Character('j') || event == Event::Character('J') ||
            event == Event::PageDown) {
            std::lock_guard<std::mutex> lock(mutex);
            ++state.scrollOffset;
            return true;
        }
        if (event == Event::Character('k') || event == Event::Character('K') ||
            event == Event::PageUp) {
            std::lock_guard<std::mutex> lock(mutex);
            if (state.scrollOffset > 0) {
                --state.scrollOffset;
            }
            return true;
        }

        return false;
    });

    screen.Loop(handler);

    keepRunning = false;
    if (pollThread.joinable()) {
        pollThread.join();
    }
    return 0;
}

}  // namespace pie
