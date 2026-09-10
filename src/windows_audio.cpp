#include "windows_audio.hpp"
#include <algorithm>
#include <cstdlib>

WindowsAudio::~WindowsAudio() { close(); }
bool WindowsAudio::open() {
    if (device) return true;
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = 48000;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = 192000;
    if (waveOutOpen(&device, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        device = nullptr; return false;
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
void WindowsAudio::reset() {
    if (!device) return;
    queued();
    waveOutReset(device);
    for (auto &buffer : buffers) buffer.pending = false;
    waveOutPause(device);
    started = false;
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
}
void WindowsAudio::submit(std::span<const std::int16_t> samples) {
    if (!device || samples.empty()) return;
    auto count = queued();
    if ((started && count == 0) || count >= 4) { reset(); count = 0; }
    while (samples.size() >= 2) {
        auto buffer = std::find_if(buffers.begin(), buffers.end(), [](const auto &b) { return !b.pending; });
        if (buffer == buffers.end()) return;
        const auto length = std::min(samples.size() & ~std::size_t(1), buffer->samples.size());
        for (std::size_t i=0; i<length; ++i) {
            // Moderate application gain; never change the system's volume.
            buffer->samples[i] = static_cast<std::int16_t>(samples[i]/2);
            peak = std::max(peak, static_cast<unsigned>(std::abs(static_cast<int>(buffer->samples[i]))));
        }
        buffer->header.dwBufferLength = static_cast<DWORD>(length*sizeof(std::int16_t));
        if (waveOutWrite(device, &buffer->header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) { close(); return; }
        buffer->pending = true;
        submitted_frames += length/2;
        ++count;
        samples = samples.subspan(length);
    }
    // Two emulated frames absorb normal scheduling jitter (~33 ms).
    if (!started && count >= 2) { waveOutRestart(device); started = true; }
}
