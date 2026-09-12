#include "macos_audio.hpp"
#include <AudioToolbox/AudioToolbox.h>
#include <dispatch/dispatch.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>

struct MacAudio::Impl {
    struct Buffer {
        AudioQueueBufferRef handle{};
        std::atomic<bool> pending{false};
    };
    AudioQueueRef device{};
    dispatch_semaphore_t completion = dispatch_semaphore_create(0);
    std::array<Buffer, 16> buffers{};
    std::array<std::int16_t, 960> staging{}; // Continuous 10 ms stereo packets.
    std::size_t staged{};
    std::atomic<unsigned> pending_count{};
    std::atomic<std::uint64_t> completed{};
    std::atomic<bool> resetting{false};
    MacAudioStats totals{};
    bool started{};
    std::chrono::steady_clock::time_point last_submit{};

    static void returned(void *opaque, AudioQueueRef, AudioQueueBufferRef handle) {
        auto &self = *static_cast<Impl *>(opaque);
        auto &buffer = *static_cast<Buffer *>(handle->mUserData);
        // A synchronous stop also returns queued buffers. Those are cancelled,
        // not completed playback, and must not count as played samples.
        if (!self.resetting.load(std::memory_order_acquire))
            self.completed.fetch_add(handle->mAudioDataByteSize / 4, std::memory_order_relaxed);
        self.pending_count.fetch_sub(1, std::memory_order_relaxed);
        buffer.pending.store(false, std::memory_order_release);
        dispatch_semaphore_signal(self.completion);
    }
    void reset() {
        if (!device) return;
        resetting.store(true, std::memory_order_release);
        AudioQueueStop(device, true); // Synchronous: no old callbacks after return.
        for (auto &buffer : buffers) buffer.pending.store(false, std::memory_order_relaxed);
        pending_count.store(0, std::memory_order_relaxed);
        started = false;
        staged = 0;
        last_submit = {};
        resetting.store(false, std::memory_order_release);
        while (dispatch_semaphore_wait(completion, DISPATCH_TIME_NOW) == 0) {}
    }
    void close() {
        if (!device) return;
        reset();
        AudioQueueDispose(device, true);
        device = nullptr;
        for (auto &buffer : buffers) buffer.handle = nullptr;
    }
    ~Impl() {
        close();
        dispatch_release(completion);
    }
};

MacAudio::MacAudio() : impl(std::make_unique<Impl>()) {}
MacAudio::~MacAudio() = default;
bool MacAudio::open() {
    auto &state = *impl;
    if (state.device) return true;
    AudioStreamBasicDescription format{};
    format.mSampleRate = 48000;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    format.mBytesPerPacket = format.mBytesPerFrame = 4;
    format.mFramesPerPacket = 1;
    format.mChannelsPerFrame = 2;
    format.mBitsPerChannel = 16;
    auto status = AudioQueueNewOutput(&format, Impl::returned, &state, nullptr, nullptr, 0, &state.device);
    if (status != noErr) { state.device = nullptr; state.totals.error = status; return false; }
    for (auto &buffer : state.buffers) {
        status = AudioQueueAllocateBuffer(state.device, sizeof(state.staging), &buffer.handle);
        if (status != noErr) { state.totals.error = status; state.close(); return false; }
        buffer.handle->mUserData = &buffer;
    }
    state.totals.error = 0;
    return true;
}
void MacAudio::reset() { impl->reset(); }
bool MacAudio::available() const { return impl->device != nullptr; }
unsigned MacAudio::queued() const { return impl->pending_count.load(std::memory_order_relaxed); }
int MacAudio::pacing_adjustment_us() const {
    if (!available() || !impl->started) return 0;
    return std::clamp((static_cast<int>(queued()) - 10) * 100, -300, 300);
}
MacAudioStats MacAudio::stats() const {
    auto result = impl->totals;
    result.completed_frames = impl->completed.load(std::memory_order_relaxed);
    result.queued_buffers = queued();
    result.available = available();
    return result;
}
void MacAudio::submit(std::span<const std::int16_t> samples) {
    auto &state = *impl;
    if (!state.device || samples.empty()) return;
    const auto now = std::chrono::steady_clock::now();
    if (state.last_submit != std::chrono::steady_clock::time_point{}) {
        const auto gap = std::chrono::duration_cast<std::chrono::milliseconds>(now - state.last_submit).count();
        state.totals.max_gap_ms = std::max(state.totals.max_gap_ms, static_cast<std::uint64_t>(gap));
    }
    state.last_submit = now;
    if (state.started && queued() == 0) {
        ++state.totals.underruns;
        AudioQueuePause(state.device);
        state.started = false;
    }
    while (samples.size() >= 2) {
        const auto count = std::min(samples.size() & ~std::size_t(1), state.staging.size() - state.staged);
        std::copy_n(samples.begin(), count, state.staging.begin() + state.staged);
        state.staged += count;
        samples = samples.subspan(count);
        if (state.staged < state.staging.size()) break;
        auto free_buffer = [&state] {
            return std::find_if(state.buffers.begin(), state.buffers.end(), [](const auto &buffer) {
                return !buffer.pending.load(std::memory_order_acquire);
            });
        };
        auto buffer = free_buffer();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
        while (buffer == state.buffers.end()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                state.totals.dropped_frames += state.staged / 2 + samples.size() / 2;
                state.totals.error = kAudioQueueErr_InvalidRunState;
                state.close();
                return;
            }
            dispatch_semaphore_wait(state.completion, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_MSEC));
            buffer = free_buffer();
        }
        auto *output = static_cast<std::int16_t *>(buffer->handle->mAudioData);
        for (std::size_t i = 0; i < state.staged; ++i) {
            output[i] = static_cast<std::int16_t>(state.staging[i] / 2);
            state.totals.peak = std::max(state.totals.peak, static_cast<unsigned>(std::abs(static_cast<int>(output[i]))));
        }
        buffer->handle->mAudioDataByteSize = static_cast<UInt32>(state.staged * sizeof(std::int16_t));
        buffer->pending.store(true, std::memory_order_release);
        state.pending_count.fetch_add(1, std::memory_order_relaxed);
        const auto status = AudioQueueEnqueueBuffer(state.device, buffer->handle, 0, nullptr);
        if (status != noErr) {
            state.pending_count.fetch_sub(1, std::memory_order_relaxed);
            buffer->pending.store(false, std::memory_order_release);
            state.totals.error = status;
            state.close();
            return;
        }
        state.totals.submitted_frames += state.staged / 2;
        state.staged = 0;
    }
    if (!state.started && queued() >= 10) {
        const auto status = AudioQueueStart(state.device, nullptr);
        if (status != noErr) { state.totals.error = status; state.close(); return; }
        state.started = true;
    }
}
