#include "windows_audio.hpp"
#include <algorithm>
#include <cstdlib>

WindowsAudio::~WindowsAudio() { close(); }
bool WindowsAudio::open() {
    if (device) return true;
    completion = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!completion) return false;
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = 48000;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = 192000;
    if (waveOutOpen(&device, WAVE_MAPPER, &format, reinterpret_cast<DWORD_PTR>(completion), 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        device = nullptr; CloseHandle(completion); completion = nullptr; return false;
    }
    for (auto &buffer : buffers) {
        buffer.header.lpData = reinterpret_cast<LPSTR>(buffer.samples.data());
        buffer.header.dwBufferLength = static_cast<DWORD>(sizeof(buffer.samples));
        if (waveOutPrepareHeader(device, &buffer.header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) { close(); return false; }
        buffer.prepared = true;
    }
    waveOutPause(device);
    return true;
}
unsigned WindowsAudio::queued() {
    unsigned count = 0;
    for (auto &buffer : buffers) {
        if (buffer.pending && (buffer.header.dwFlags & WHDR_DONE)) {
            completed_frames += buffer.header.dwBufferLength / 4;
            buffer.pending = false;
        }
        if (buffer.pending) ++count;
    }
    return count;
}
int WindowsAudio::pacing_adjustment_us() {
    if (!device || !started) return 0;
    // Keep the emulator's wall clock aligned with the physical audio clock.
    // Small corrections prevent slow drift from emptying a healthy buffer.
    return std::clamp((static_cast<int>(queued()) - 10) * 100, -300, 300);
}
void WindowsAudio::reset() {
    if (!device) return;
    queued();
    waveOutReset(device);
    for (auto &buffer : buffers) buffer.pending = false;
    waveOutPause(device);
    started = false;
    staged = 0;
    last_submit_ms = 0;
}
void WindowsAudio::close() {
    if (!device) return;
    reset();
    for (auto &buffer : buffers) {
        if (buffer.prepared) waveOutUnprepareHeader(device, &buffer.header, sizeof(WAVEHDR));
        buffer.prepared = false;
    }
    waveOutClose(device);
    device = nullptr;
    if (completion) CloseHandle(completion);
    completion = nullptr;
}
void WindowsAudio::submit(std::span<const std::int16_t> samples) {
    if (!device || samples.empty()) return;
    const auto now = GetTickCount64();
    const auto gap = last_submit_ms ? now - last_submit_ms : 0;
    max_gap_ms = std::max(max_gap_ms, gap);
    last_submit_ms = now;
    auto count = queued();
    if (started && count == 0) {
        // Recover once from actual starvation. Never discard a healthy queue
        // just because a catch-up frame temporarily adds more sound.
        ++underruns;
        underrun_gap_ms = gap;
        waveOutPause(device);
        started = false;
    }
    while (samples.size() >= 2) {
        const auto length = std::min(samples.size() & ~std::size_t(1), staging.size() - staged);
        std::copy_n(samples.begin(), length, staging.begin() + staged);
        staged += length;
        samples = samples.subspan(length);
        if (staged != staging.size()) break;
        auto buffer = std::find_if(buffers.begin(), buffers.end(), [](const auto &b) { return !b.pending; });
        // Apply backpressure when the device clock falls slightly behind the
        // emulator. Wait for a completed packet instead of throwing audio away.
        const auto deadline = GetTickCount64() + 250;
        while (buffer == buffers.end()) {
            if (GetTickCount64() >= deadline || WaitForSingleObject(completion, 20) == WAIT_FAILED) {
                dropped_frames += staged / 2 + samples.size() / 2;
                close();
                return;
            }
            count = queued();
            buffer = std::find_if(buffers.begin(), buffers.end(), [](const auto &b) { return !b.pending; });
        }
        for (std::size_t i = 0; i < staged; ++i) {
            buffer->samples[i] = static_cast<std::int16_t>(staging[i] / 2);
            peak = std::max(peak, static_cast<unsigned>(std::abs(static_cast<int>(buffer->samples[i]))));
        }
        buffer->header.dwBufferLength = static_cast<DWORD>(staged * sizeof(std::int16_t));
        if (waveOutWrite(device, &buffer->header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) { close(); return; }
        buffer->pending = true;
        submitted_frames += staged / 2;
        staged = 0;
        ++count;
    }
    // Fixed 10 ms packets preserve continuity across emulator frame boundaries.
    // Prime 100 ms for host jitter; unused capacity accommodates catch-up frames.
    if (!started && count >= 10) { waveOutRestart(device); started = true; }
}
