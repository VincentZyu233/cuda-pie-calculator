#include "tui.hpp"

#include "resources.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace pie {
namespace {

enum class Key {
    None,
    Start,
    Pause,
    Cancel,
    Quit,
    ToggleMode,
    IncreasePrecision,
    DecreasePrecision,
    ScrollUp,
    ScrollDown,
};

struct TerminalSize {
    unsigned rows = 30;
    unsigned columns = 100;
};

class TerminalSession {
public:
    TerminalSession() {
#if defined(_WIN32)
        input_ = GetStdHandle(STD_INPUT_HANDLE);
        output_ = GetStdHandle(STD_OUTPUT_HANDLE);
        if (input_ == INVALID_HANDLE_VALUE || output_ == INVALID_HANDLE_VALUE ||
            GetConsoleMode(input_, &oldInputMode_) == 0 || GetConsoleMode(output_, &oldOutputMode_) == 0) {
            throw std::runtime_error("A Windows console is required to run the TUI.");
        }
        const DWORD newInputMode = (oldInputMode_ | ENABLE_WINDOW_INPUT) & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
        const DWORD newOutputMode = oldOutputMode_ | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        if (SetConsoleMode(input_, newInputMode) == 0 || SetConsoleMode(output_, newOutputMode) == 0) {
            throw std::runtime_error("Windows 10/11 virtual terminal support could not be enabled.");
        }
        SetConsoleOutputCP(CP_UTF8);
#else
        if (isatty(STDIN_FILENO) == 0 || isatty(STDOUT_FILENO) == 0) {
            throw std::runtime_error("An interactive terminal is required to run the TUI.");
        }
        if (tcgetattr(STDIN_FILENO, &oldAttributes_) != 0) {
            throw std::runtime_error("Unable to read terminal settings.");
        }
        termios raw = oldAttributes_;
        raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
            throw std::runtime_error("Unable to enable terminal raw input mode.");
        }
        oldInputFlags_ = fcntl(STDIN_FILENO, F_GETFL, 0);
        if (oldInputFlags_ >= 0) {
            fcntl(STDIN_FILENO, F_SETFL, oldInputFlags_ | O_NONBLOCK);
        }
#endif
        active_ = true;
        std::cout << "\x1b[?1049h\x1b[2J\x1b[H\x1b[?25l" << std::flush;
    }

    ~TerminalSession() {
        if (!active_) {
            return;
        }
        std::cout << "\x1b[?25h\x1b[0m\x1b[?1049l" << std::flush;
#if defined(_WIN32)
        SetConsoleMode(input_, oldInputMode_);
        SetConsoleMode(output_, oldOutputMode_);
#else
        tcsetattr(STDIN_FILENO, TCSANOW, &oldAttributes_);
        if (oldInputFlags_ >= 0) {
            fcntl(STDIN_FILENO, F_SETFL, oldInputFlags_);
        }
#endif
    }

    TerminalSize size() const {
        TerminalSize result;
#if defined(_WIN32)
        CONSOLE_SCREEN_BUFFER_INFO info{};
        if (GetConsoleScreenBufferInfo(output_, &info) != 0) {
            result.columns = static_cast<unsigned>(info.srWindow.Right - info.srWindow.Left + 1);
            result.rows = static_cast<unsigned>(info.srWindow.Bottom - info.srWindow.Top + 1);
        }
#else
        winsize window{};
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &window) == 0) {
            if (window.ws_col > 0) {
                result.columns = window.ws_col;
            }
            if (window.ws_row > 0) {
                result.rows = window.ws_row;
            }
        }
#endif
        result.columns = std::max(result.columns, 40U);
        result.rows = std::max(result.rows, 12U);
        return result;
    }

    Key pollKey() {
#if defined(_WIN32)
        INPUT_RECORD record{};
        DWORD pending = 0;
        while (GetNumberOfConsoleInputEvents(input_, &pending) != 0 && pending > 0) {
            DWORD received = 0;
            if (ReadConsoleInputW(input_, &record, 1, &received) == 0 || received == 0) {
                break;
            }
            if (record.EventType != KEY_EVENT || record.Event.KeyEvent.bKeyDown == 0) {
                continue;
            }
            const WORD virtualKey = record.Event.KeyEvent.wVirtualKeyCode;
            if (virtualKey == VK_UP) return Key::ScrollUp;
            if (virtualKey == VK_DOWN) return Key::ScrollDown;
            const wchar_t character = record.Event.KeyEvent.uChar.UnicodeChar;
            return mapCharacter(static_cast<char>(character));
        }
#else
        char input[32]{};
        const ssize_t count = read(STDIN_FILENO, input, sizeof(input));
        if (count <= 0) {
            return Key::None;
        }
        for (ssize_t index = 0; index < count; ++index) {
            if (input[index] == '\x1b' && index + 2 < count && input[index + 1] == '[') {
                if (input[index + 2] == 'A') return Key::ScrollUp;
                if (input[index + 2] == 'B') return Key::ScrollDown;
            }
            const Key key = mapCharacter(input[index]);
            if (key != Key::None) {
                return key;
            }
        }
#endif
        return Key::None;
    }

private:
    static Key mapCharacter(char character) {
        switch (character) {
            case 's': case 'S': return Key::Start;
            case 'p': case 'P': return Key::Pause;
            case 'c': case 'C': return Key::Cancel;
            case 'm': case 'M': return Key::ToggleMode;
            case 'q': case 'Q': return Key::Quit;
            case '+': case '=': return Key::IncreasePrecision;
            case '-': case '_': return Key::DecreasePrecision;
            case 'k': case 'K': return Key::ScrollUp;
            case 'j': case 'J': return Key::ScrollDown;
            default: return Key::None;
        }
    }

    bool active_ = false;
#if defined(_WIN32)
    HANDLE input_ = INVALID_HANDLE_VALUE;
    HANDLE output_ = INVALID_HANDLE_VALUE;
    DWORD oldInputMode_ = 0;
    DWORD oldOutputMode_ = 0;
#else
    termios oldAttributes_{};
    int oldInputFlags_ = -1;
#endif
};

std::string crop(std::string value, std::size_t width) {
    if (value.size() <= width) {
        return value;
    }
    if (width <= 3) {
        return value.substr(0, width);
    }
    value.resize(width - 3);
    return value + "...";
}

void appendLine(std::ostringstream& output, const std::string& text, std::size_t width, bool advance = true) {
    const std::string visible = crop(text, width);
    output << visible;
    if (visible.size() < width) {
        output << std::string(width - visible.size(), ' ');
    }
    output << "\x1b[K";
    if (advance) {
        output << "\r\n";
    }
}

std::string progressBar(unsigned completed, unsigned total, std::size_t availableWidth) {
    const std::size_t barWidth = std::clamp<std::size_t>(availableWidth, 8, 32);
    const double ratio = total == 0 ? 0.0 : std::min(1.0, static_cast<double>(completed) / static_cast<double>(total));
    const std::size_t filled = static_cast<std::size_t>(ratio * static_cast<double>(barWidth));
    std::ostringstream output;
    output << '[' << std::string(filled, '#') << std::string(barWidth - filled, '-') << "] "
           << std::setw(3) << static_cast<unsigned>(ratio * 100.0) << '%';
    return output.str();
}

bool jobIsActive(JobState state) {
    return state == JobState::Preparing || state == JobState::Running || state == JobState::Paused || state == JobState::Cancelling;
}

std::string formatSampleCount(std::uint64_t samples) {
    std::ostringstream output;
    if (samples >= 1'000'000'000ULL) {
        output << std::fixed << std::setprecision(2) << static_cast<double>(samples) / 1'000'000'000.0 << " B";
    } else if (samples >= 1'000'000ULL) {
        output << std::fixed << std::setprecision(1) << static_cast<double>(samples) / 1'000'000.0 << " M";
    } else {
        output << samples;
    }
    return output.str();
}

std::vector<std::string> resultLines(const std::string& result, std::size_t width, CalculationMode selectedMode) {
    std::vector<std::string> lines;
    if (result.empty()) {
        lines.emplace_back(selectedMode == CalculationMode::MonteCarlo
                               ? "Waiting for CUDA Monte Carlo samples. No CPU calculation path exists."
                               : "Waiting for a CUDA exact result. No CPU calculation path exists.");
        return lines;
    }

    std::size_t segmentStart = 0;
    while (segmentStart <= result.size()) {
        const std::size_t lineBreak = result.find('\n', segmentStart);
        const std::size_t segmentEnd = lineBreak == std::string::npos ? result.size() : lineBreak;
        const std::string segment = result.substr(segmentStart, segmentEnd - segmentStart);
        if (segment.empty()) {
            lines.emplace_back();
        } else {
            for (std::size_t offset = 0; offset < segment.size(); offset += width) {
                lines.push_back(segment.substr(offset, width));
            }
        }
        if (lineBreak == std::string::npos) {
            break;
        }
        segmentStart = lineBreak + 1;
    }
    return lines;
}

std::string cpuLine(const ResourceStats& stats) {
    std::ostringstream output;
    output << "CPU: ";
    if (stats.cpuAvailable) {
        output << std::fixed << std::setprecision(1) << stats.cpuPercent << '%';
    } else {
        output << "sampling";
    }
    output << "   RAM: " << formatBytes(stats.memoryUsedBytes) << " / " << formatBytes(stats.memoryTotalBytes);
    return output.str();
}

std::string gpuLoadLine(const ResourceStats& stats) {
    if (!stats.gpuDetailsAvailable) {
        return "GPU load: NVML metrics unavailable";
    }
    std::ostringstream output;
    output << "GPU load: " << stats.gpuUtilizationPercent << "%   VRAM: "
           << formatBytes(stats.gpuMemoryUsedBytes) << " / " << formatBytes(stats.gpuMemoryTotalBytes)
           << "   Temp: " << stats.gpuTemperatureCelsius << " C";
    return output.str();
}

void render(
    const TerminalSize& terminal,
    const GpuInfo& gpu,
    const JobSnapshot& job,
    const ResourceStats& resources,
    CalculationMode selectedMode,
    unsigned selectedDigits,
    std::uint64_t selectedSamples,
    std::size_t& scrollOffset) {
    const std::size_t width = terminal.columns;
    const std::size_t bottomRows = 5;
    const std::size_t middleRows = std::max<std::size_t>(1, terminal.rows - bottomRows - 4);
    const auto lines = resultLines(job.result, width - 2, selectedMode);
    const std::size_t maxScroll = lines.size() > middleRows ? lines.size() - middleRows : 0;
    scrollOffset = std::min(scrollOffset, maxScroll);

    std::ostringstream output;
    output << "\x1b[H\x1b[1;36m";
    appendLine(output, "CUDA Pi Calculator", width);
    output << "\x1b[0m";
    if (gpu.available) {
        std::ostringstream gpuLine;
        gpuLine << "CUDA GPU: " << gpu.name << "  CC " << gpu.computeMajor << '.' << gpu.computeMinor
                << "  VRAM " << formatBytes(gpu.totalMemoryBytes);
        appendLine(output, gpuLine.str(), width);
    } else {
        appendLine(output, gpu.reason, width);
    }

    std::ostringstream status;
    status << "State: " << stateLabel(job.state) << "   Mode: " << modeLabel(selectedMode) << "   ";
    if (selectedMode == CalculationMode::MonteCarlo) {
        status << "Target: " << formatSampleCount(selectedSamples) << " samples   ";
    } else {
        status << "Precision: " << selectedDigits << " digits   ";
    }
    status << progressBar(job.completedSteps, job.totalSteps, width / 3);
    appendLine(output, status.str(), width);
    const CalculationMode outputMode = job.result.empty() ? selectedMode : job.mode;
    appendLine(output, outputMode == CalculationMode::MonteCarlo ? "Monte Carlo output" : "Exact Pi output", width);

    for (std::size_t row = 0; row < middleRows; ++row) {
        const std::size_t line = scrollOffset + row;
        appendLine(output, line < lines.size() ? " " + lines[line] : "", width);
    }

    std::string phase = job.phase.empty() ? job.message : job.phase;
    if (job.mode == CalculationMode::MonteCarlo && job.samplesCompleted > 0) {
        phase += "   Samples: " + formatSampleCount(job.samplesCompleted) + " / " + formatSampleCount(job.sampleTarget) +
                 "   Hits: " + std::to_string(job.hitsInsideCircle);
    }
    appendLine(output, phase, width);
    appendLine(output, cpuLine(resources), width);
    appendLine(output, gpuLoadLine(resources), width);
    if (gpu.available) {
        appendLine(output, "[s] start  [m] mode  [p] pause/resume  [c] cancel  [+/-] target  [j/k or arrows] scroll  [q] quit", width);
    } else {
        appendLine(output, "[s] disabled: no CUDA GPU   [q] quit", width);
    }
    if (job.gpuMilliseconds > 0.0) {
        std::ostringstream timing;
        timing << "CUDA kernel time: " << std::fixed << std::setprecision(2) << job.gpuMilliseconds << " ms  " << job.message;
        appendLine(output, timing.str(), width, false);
    } else {
        appendLine(output, job.message, width, false);
    }

    std::cout << output.str() << std::flush;
}

}  // namespace

int TerminalUi::run(PiEngine& engine) {
    TerminalSession terminal;
    ResourceMonitor monitor(engine.gpu());
    CalculationMode selectedMode = CalculationMode::ExactDigits;
    unsigned selectedDigits = 1000;
    std::uint64_t selectedSamples = 100'000'000ULL;
    std::size_t scrollOffset = 0;
    bool running = true;

    while (running) {
        const Key key = terminal.pollKey();
        switch (key) {
            case Key::Start:
                if (selectedMode == CalculationMode::MonteCarlo) {
                    engine.startMonteCarlo(selectedSamples);
                } else {
                    engine.startExact(selectedDigits);
                }
                scrollOffset = 0;
                break;
            case Key::ToggleMode: {
                const JobState state = engine.snapshot().state;
                if (!jobIsActive(state)) {
                    selectedMode = selectedMode == CalculationMode::ExactDigits ? CalculationMode::MonteCarlo : CalculationMode::ExactDigits;
                    scrollOffset = 0;
                }
                break;
            }
            case Key::Pause:
                engine.togglePause();
                break;
            case Key::Cancel:
                engine.cancel();
                break;
            case Key::IncreasePrecision:
                if (jobIsActive(engine.snapshot().state)) {
                    break;
                }
                if (selectedMode == CalculationMode::MonteCarlo) {
                    selectedSamples = std::min<std::uint64_t>(kMaximumMonteCarloSamples, selectedSamples + 10'000'000ULL);
                } else {
                    selectedDigits = std::min(kMaximumDigits, selectedDigits + 100U);
                }
                break;
            case Key::DecreasePrecision:
                if (jobIsActive(engine.snapshot().state)) {
                    break;
                }
                if (selectedMode == CalculationMode::MonteCarlo) {
                    selectedSamples = selectedSamples > kMinimumMonteCarloSamples + 10'000'000ULL
                                          ? selectedSamples - 10'000'000ULL
                                          : kMinimumMonteCarloSamples;
                } else {
                    selectedDigits = std::max(kMinimumDigits, selectedDigits > 100U ? selectedDigits - 100U : kMinimumDigits);
                }
                break;
            case Key::ScrollUp:
                scrollOffset = scrollOffset == 0 ? 0 : scrollOffset - 1;
                break;
            case Key::ScrollDown:
                ++scrollOffset;
                break;
            case Key::Quit:
                engine.stop();
                running = false;
                break;
            case Key::None:
                break;
        }

        render(terminal.size(), engine.gpu(), engine.snapshot(), monitor.sample(), selectedMode, selectedDigits, selectedSamples, scrollOffset);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return 0;
}

}  // namespace pie
