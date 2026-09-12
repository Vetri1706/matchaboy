#pragma once
#include <cstdint>
#include <memory>
#include <span>

struct MacAudioStats {
    std::uint64_t submitted_frames{}, completed_frames{}, underruns{}, dropped_frames{}, max_gap_ms{};
    unsigned peak{}, queued_buffers{};
    bool available{};
    std::int32_t error{};
};

// 48 kHz, signed 16-bit interleaved stereo through Apple's Audio Queue.
// Call the public API from the main thread; the internal completion callback
// only publishes atomic buffer ownership/counters and signals a semaphore.
class MacAudio {
public:
    MacAudio();
    ~MacAudio();
    MacAudio(const MacAudio &) = delete;
    MacAudio &operator=(const MacAudio &) = delete;
    bool open();
    void reset();
    void submit(std::span<const std::int16_t> samples);
    unsigned queued() const;
    int pacing_adjustment_us() const;
    bool available() const;
    MacAudioStats stats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
