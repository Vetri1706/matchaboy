#include "linux_audio.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#ifdef MATCHA_LINUX_ALSA
#include <alsa/asoundlib.h>
#endif
struct LinuxAudio::Impl {
    std::string message = "Audio unavailable in this build";
#ifdef MATCHA_LINUX_ALSA
    snd_pcm_t *device{};
    std::array<std::int16_t, 8192> queued{};
    std::size_t head{}, count{};
    Impl() {
        int error = snd_pcm_open(&device, "default", SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
        if (error < 0) {
            device = nullptr;
            message = std::string("Audio unavailable: ") + snd_strerror(error);
            return;
        }
        error = snd_pcm_set_params(device, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2,
                                   48000, 1, 80000);
        if (error < 0) {
            message = std::string("Audio unavailable: ") + snd_strerror(error);
            snd_pcm_close(device);
            device = nullptr;
            return;
        }
        message = "48 kHz stereo";
    }
    ~Impl() {
        if (device)
            snd_pcm_close(device);
    }
#else
    Impl() = default;
#endif
};
LinuxAudio::LinuxAudio() : impl_(std::make_unique<Impl>()) {
}
LinuxAudio::~LinuxAudio() = default;
void LinuxAudio::push(std::span<const std::int16_t> samples) {
#ifdef MATCHA_LINUX_ALSA
    if (!impl_->device)
        return;
    auto &state = *impl_;
    const auto capacity = state.queued.size();
    samples = samples.first(samples.size() & ~std::size_t{1});
    if (samples.size() > capacity)
        samples = samples.last(capacity);
    const auto incoming = samples.size();
    if (state.count + incoming > capacity) {
        // Bound host latency if the audio device cannot keep up. Discard whole
        // oldest stereo frames; never stall deterministic emulation or UDP.
        const auto discard = state.count + incoming - capacity;
        state.head = (state.head + discard) % capacity;
        state.count -= discard;
    }
    for (std::size_t i = 0; i < incoming; ++i)
        state.queued[(state.head + state.count + i) % capacity] = samples[i];
    state.count += incoming;
    // Preserve positive short writes and EAGAIN tails for the next host tick.
    // The bounded attempts also keep a recovering device off the critical path.
    for (unsigned attempt = 0; attempt < 3 && state.count; ++attempt) {
        const auto contiguous = std::min(state.count, capacity - state.head);
        const auto result =
            snd_pcm_writei(state.device, state.queued.data() + state.head, contiguous / 2);
        if (result > 0) {
            const auto accepted = static_cast<std::size_t>(result) * 2;
            state.head = (state.head + accepted) % capacity;
            state.count -= accepted;
        } else if (result == 0 || result == -EAGAIN) {
            break;
        } else {
            if (result == -EINTR)
                continue;
            int recovered = static_cast<int>(result);
            if (result == -EPIPE)
                recovered = snd_pcm_prepare(state.device);
            else if (result == -ESTRPIPE) {
                recovered = snd_pcm_resume(state.device);
                if (recovered == -EAGAIN)
                    break;
                if (recovered < 0)
                    recovered = snd_pcm_prepare(state.device);
            }
            if (recovered < 0) {
                state.message = std::string("Audio unavailable: ") + snd_strerror(recovered);
                snd_pcm_close(state.device);
                state.device = nullptr;
                state.head = state.count = 0;
                break;
            }
        }
    }
#else
    (void)samples;
#endif
}
void LinuxAudio::clear() {
#ifdef MATCHA_LINUX_ALSA
    impl_->head = impl_->count = 0;
    if (impl_->device) {
        snd_pcm_drop(impl_->device);
        snd_pcm_prepare(impl_->device);
    }
#endif
}
std::string LinuxAudio::status() const {
    return impl_->message;
}

bool LinuxAudio::available() const {
#ifdef MATCHA_LINUX_ALSA
    return impl_->device != nullptr;
#else
    return false;
#endif
}
