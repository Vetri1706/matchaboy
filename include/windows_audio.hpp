#pragma once
#include <windows.h>
#include <mmsystem.h>
#include <array>
#include <cstdint>
#include <span>

// Asynchronous PCM output through Windows itself; no audio middleware/DLLs.
class WindowsAudio {
public:
    ~WindowsAudio();
    bool open();
    void reset();
    void submit(std::span<const std::int16_t> samples);
    unsigned queued();
    int pacing_adjustment_us();
    bool available() const { return device != nullptr; }
    std::uint64_t submitted_frames = 0, completed_frames = 0;
    unsigned peak = 0;
    std::uint64_t underruns = 0, dropped_frames = 0, max_gap_ms = 0, last_submit_ms = 0, underrun_gap_ms = 0;
private:
    struct Buffer {
        WAVEHDR header{};
        std::array<std::int16_t, 960> samples{};
        bool prepared = false, pending = false;
    };
    HWAVEOUT device{};
    HANDLE completion{};
    std::array<Buffer, 16> buffers{};
    bool started = false;
    std::array<std::int16_t, 960> staging{};
    std::size_t staged = 0;
    void close();
};
