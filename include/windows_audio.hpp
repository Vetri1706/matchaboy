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
    bool available() const { return device != nullptr; }
    std::uint64_t submitted_frames = 0, completed_frames = 0;
    unsigned peak = 0;
private:
    struct Buffer {
        WAVEHDR header{};
        std::array<std::int16_t, 2048> samples{};
        bool prepared = false, pending = false;
    };
    HWAVEOUT device{};
    std::array<Buffer, 6> buffers{};
    bool started = false;
    void close();
};
