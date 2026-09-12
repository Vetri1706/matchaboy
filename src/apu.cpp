#include "dmg/apu.hpp"
#include <algorithm>
#include <cmath>

namespace dmg {
namespace {
constexpr std::uint32_t clock_rate = 4'194'304;
constexpr std::array<std::uint8_t, 0x17> read_masks{
    0x80, 0x3F, 0x00, 0xFF, 0xBF, 0xFF, 0x3F, 0x00,
    0xFF, 0xBF, 0x7F, 0xFF, 0x9F, 0xFF, 0xBF, 0xFF,
    0xFF, 0x00, 0x00, 0xBF, 0x00, 0x00, 0x70};
constexpr std::array<unsigned, 4> control_register{4, 9, 14, 19};
constexpr std::array<std::uint8_t, 4> duty_patterns{0x01, 0x81, 0x87, 0x7E};
}

void Apu::initialize_postboot() {
    state_ = {};
    state_.powered = true;
    state_.registers[1] = 0x80;
    state_.registers[2] = 0xF3;
    state_.registers[3] = 0xC1;
    state_.registers[4] = 0x07;
    state_.registers[20] = 0x77;
    state_.registers[21] = 0xF3;
    state_.pulse[0].length = 1;
    trigger(0);
}

std::uint16_t Apu::frequency(unsigned channel) const {
    const unsigned lo = channel == 0 ? 3 : channel == 1 ? 8 : 13;
    return static_cast<std::uint16_t>(state_.registers[lo] |
        ((state_.registers[lo + 1] & 7U) << 8));
}

std::uint32_t Apu::noise_period() const {
    constexpr std::array<std::uint32_t, 8> divisors{8, 16, 32, 48, 64, 80, 96, 112};
    const auto control = state_.registers[18];
    return divisors[control & 7U] << (control >> 4);
}

bool Apu::dac_enabled(unsigned channel) const {
    if (channel == 2) return (state_.registers[10] & 0x80) != 0;
    const unsigned envelope = channel == 0 ? 2 : channel == 1 ? 7 : 17;
    return (state_.registers[envelope] & 0xF8) != 0;
}

std::uint8_t Apu::read(std::uint16_t address) const {
    if (address >= 0xFF30 && address <= 0xFF3F) {
        if (state_.wave.enabled)
            return state_.wave.access_ticks ? state_.wave_ram[state_.wave.position / 2] : 0xFF;
        return state_.wave_ram[address - 0xFF30];
    }
    if (address < 0xFF10 || address > 0xFF26) return 0xFF;
    if (address == 0xFF26) {
        return static_cast<std::uint8_t>(0x70 | (state_.powered ? 0x80 : 0) |
            (state_.pulse[0].enabled ? 1 : 0) | (state_.pulse[1].enabled ? 2 : 0) |
            (state_.wave.enabled ? 4 : 0) | (state_.noise.enabled ? 8 : 0));
    }
    const auto index = static_cast<std::size_t>(address - 0xFF10);
    return static_cast<std::uint8_t>(state_.registers[index] | read_masks[index]);
}

void Apu::write(std::uint16_t address, std::uint8_t value) {
    if (address >= 0xFF30 && address <= 0xFF3F) {
        if (!state_.wave.enabled) state_.wave_ram[address - 0xFF30] = value;
        else if (state_.wave.access_ticks) state_.wave_ram[state_.wave.position / 2] = value;
        return;
    }
    if (address < 0xFF10 || address > 0xFF26) return;
    if (address == 0xFF26) {
        const bool power = (value & 0x80) != 0;
        if (power == state_.powered) return;
        state_.powered = power;
        state_.frame_step = 0;
        if (!power) {
            state_.registers.fill(0);
            for (auto &pulse : state_.pulse) {
                const auto length = pulse.length;
                pulse = {};
                pulse.length = length;
            }
            const auto wave_length = state_.wave.length;
            state_.wave = {};
            state_.wave.length = wave_length;
            const auto noise_length = state_.noise.length;
            state_.noise = {};
            state_.noise.length = noise_length;
            state_.highpass_capacitor.fill(0);
        }
        return;
    }
    const auto index = static_cast<unsigned>(address - 0xFF10);
    if (index == 5 || index == 15) return;
    // On DMG the four length counters remain writable while NR52 is off.
    if (!state_.powered && index != 1 && index != 6 && index != 11 && index != 16) return;
    const auto previous = state_.registers[index];
    if (state_.powered) state_.registers[index] = value;
    if (index == 1 || index == 6)
        state_.pulse[index == 1 ? 0 : 1].length = static_cast<std::uint16_t>(64 - (value & 63));
    else if (index == 11) state_.wave.length = static_cast<std::uint16_t>(256 - value);
    else if (index == 16) state_.noise.length = static_cast<std::uint16_t>(64 - (value & 63));
    if (!state_.powered) return;
    if (index == 0 && (previous & 8) && !(value & 8) && state_.pulse[0].sweep_negated)
        state_.pulse[0].enabled = false;
    if (index == 2 || index == 7 || index == 17) {
        const unsigned channel = index == 2 ? 0 : index == 7 ? 1 : 3;
        auto &envelope = channel == 3 ? state_.noise.envelope : state_.pulse[channel].envelope;
        bool &enabled = channel == 3 ? state_.noise.enabled : state_.pulse[channel].enabled;
        // The documented common DMG zombie-envelope case.
        if (enabled && (previous & 15) == 8 && (value & 15) == 8)
            envelope.volume = static_cast<std::uint8_t>((envelope.volume + 1) & 15);
        if (!dac_enabled(channel)) enabled = false;
    }
    if (index == 10 && !dac_enabled(2)) state_.wave.enabled = false;
    for (unsigned channel = 0; channel < 4; ++channel) {
        if (index != control_register[channel]) continue;
        auto &length = channel < 2 ? state_.pulse[channel].length :
            channel == 2 ? state_.wave.length : state_.noise.length;
        bool &enabled = channel < 2 ? state_.pulse[channel].enabled :
            channel == 2 ? state_.wave.enabled : state_.noise.enabled;
        if (!(previous & 0x40) && (value & 0x40) && (state_.frame_step & 1) && length != 0) {
            --length;
            if (length == 0) enabled = false;
        }
        if (value & 0x80) trigger(channel);
        state_.registers[index] &= 0x7F;
    }
}

void Apu::trigger(unsigned channel) {
    if (channel >= control_register.size()) return;
    auto &length = channel < 2 ? state_.pulse[channel].length :
        channel == 2 ? state_.wave.length : state_.noise.length;
    if (length == 0) {
        length = channel == 2 ? 256 : 64;
        if ((state_.registers[control_register[channel]] & 0x40) && (state_.frame_step & 1)) --length;
    }
    if (channel < 2) {
        auto &pulse = state_.pulse[channel];
        pulse.enabled = dac_enabled(channel);
        pulse.triggered = true;
        // The low two divider bits survive a retrigger.
        pulse.timer = 4U * (2048U - frequency(channel)) + (pulse.timer & 3U);
    } else if (channel == 2) {
        auto &wave = state_.wave;
        // The retrigger strobe collides with the upcoming wave-RAM address
        // phase, two T-cycles before sample transfer. CPU read/write access
        // uses a separate window after transfer (access_ticks).
        if (wave.enabled && wave.timer == 2) {
            const unsigned byte = ((wave.position + 1) & 31) / 2;
            if (byte < 4) state_.wave_ram[0] = state_.wave_ram[byte];
            else {
                const auto old = state_.wave_ram;
                for (unsigned i = 0; i < 4; ++i) state_.wave_ram[i] = old[(byte & ~3U) + i];
            }
        }
        wave.enabled = dac_enabled(2);
        wave.position = 0;
        wave.access_ticks = 0;
        wave.timer = 2U * (2048U - frequency(2)) + 6U;
    } else {
        state_.noise.enabled = dac_enabled(3);
        state_.noise.lfsr = 0x7FFF;
        state_.noise.timer = noise_period();
    }
    if (channel != 2) {
        auto &envelope = channel == 3 ? state_.noise.envelope : state_.pulse[channel].envelope;
        const auto control = state_.registers[channel == 0 ? 2 : channel == 1 ? 7 : 17];
        envelope.volume = control >> 4;
        envelope.timer = static_cast<std::uint8_t>((control & 7) ? control & 7 : 8);
        if (state_.frame_step == 7) ++envelope.timer;
        envelope.running = true;
    }
    if (channel == 0) {
        auto &pulse = state_.pulse[0];
        const auto sweep = state_.registers[0];
        pulse.sweep_shadow = frequency(0);
        pulse.sweep_timer = static_cast<std::uint8_t>((sweep >> 4) & 7);
        pulse.sweep_enabled = pulse.sweep_timer != 0 || (sweep & 7) != 0;
        if (pulse.sweep_timer == 0) pulse.sweep_timer = 8;
        pulse.sweep_negated = false;
        if ((sweep & 7) && sweep_calculate() > 2047) pulse.enabled = false;
    }
}

std::uint16_t Apu::sweep_calculate() {
    auto &pulse = state_.pulse[0];
    const auto sweep = state_.registers[0];
    const auto change = static_cast<std::uint16_t>(pulse.sweep_shadow >> (sweep & 7));
    if (sweep & 8) {
        pulse.sweep_negated = true;
        return static_cast<std::uint16_t>(pulse.sweep_shadow - change);
    }
    return static_cast<std::uint16_t>(pulse.sweep_shadow + change);
}

void Apu::clock_sweep() {
    auto &pulse = state_.pulse[0];
    if (pulse.sweep_timer != 0) --pulse.sweep_timer;
    if (pulse.sweep_timer != 0) return;
    const auto period = static_cast<std::uint8_t>((state_.registers[0] >> 4) & 7);
    pulse.sweep_timer = period ? period : 8;
    if (!pulse.sweep_enabled || period == 0) return;
    const auto next = sweep_calculate();
    if (next > 2047) { pulse.enabled = false; return; }
    if ((state_.registers[0] & 7) == 0) return;
    pulse.sweep_shadow = next;
    state_.registers[3] = static_cast<std::uint8_t>(next);
    state_.registers[4] = static_cast<std::uint8_t>((state_.registers[4] & 0xF8) | (next >> 8));
    if (sweep_calculate() > 2047) pulse.enabled = false;
}

void Apu::clock_length() {
    for (unsigned channel = 0; channel < 4; ++channel) {
        auto &length = channel < 2 ? state_.pulse[channel].length :
            channel == 2 ? state_.wave.length : state_.noise.length;
        if (!(state_.registers[control_register[channel]] & 0x40) || length == 0) continue;
        --length;
        if (length != 0) continue;
        if (channel < 2) state_.pulse[channel].enabled = false;
        else if (channel == 2) state_.wave.enabled = false;
        else state_.noise.enabled = false;
    }
}

void Apu::clock_envelope(EnvelopeState &envelope, std::uint8_t control) {
    if (envelope.timer != 0) --envelope.timer;
    if (envelope.timer != 0) return;
    envelope.timer = static_cast<std::uint8_t>((control & 7) ? control & 7 : 8);
    if (!envelope.running || (control & 7) == 0) return;
    const int next = static_cast<int>(envelope.volume) + ((control & 8) ? 1 : -1);
    if (next < 0 || next > 15) envelope.running = false;
    else envelope.volume = static_cast<std::uint8_t>(next);
}

void Apu::frame_sequencer_tick() {
    if (!state_.powered) return;
    if ((state_.frame_step & 1) == 0) clock_length();
    if (state_.frame_step == 2 || state_.frame_step == 6) clock_sweep();
    if (state_.frame_step == 7) {
        clock_envelope(state_.pulse[0].envelope, state_.registers[2]);
        clock_envelope(state_.pulse[1].envelope, state_.registers[7]);
        clock_envelope(state_.noise.envelope, state_.registers[17]);
    }
    state_.frame_step = static_cast<std::uint8_t>((state_.frame_step + 1) & 7);
}

void Apu::tick() {
    ++state_.t_cycles;
    if (state_.powered) {
        for (unsigned channel = 0; channel < 2; ++channel) {
            auto &pulse = state_.pulse[channel];
            if (!pulse.triggered) continue;
            if (pulse.timer != 0) --pulse.timer;
            if (pulse.timer == 0) {
                pulse.timer = 4U * (2048U - frequency(channel));
                pulse.position = static_cast<std::uint8_t>((pulse.position + 1) & 7);
                pulse.duty_started = true;
            }
        }
        auto &wave = state_.wave;
        if (wave.access_ticks) --wave.access_ticks;
        if (wave.enabled) {
            if (wave.timer) --wave.timer;
            if (wave.timer == 0) {
                wave.timer = 2U * (2048U - frequency(2));
                wave.position = static_cast<std::uint8_t>((wave.position + 1) & 31);
                const auto byte = state_.wave_ram[wave.position / 2];
                wave.sample = static_cast<std::uint8_t>((wave.position & 1) ? byte & 15 : byte >> 4);
                wave.access_ticks = 2;
            }
        }
        auto &noise = state_.noise;
        if (noise.enabled && (state_.registers[18] >> 4) < 14) {
            if (noise.timer) --noise.timer;
            if (noise.timer == 0) {
                noise.timer = noise_period();
                const unsigned feedback = (noise.lfsr ^ (noise.lfsr >> 1)) & 1;
                noise.lfsr = static_cast<std::uint16_t>((noise.lfsr >> 1) | (feedback << 14));
                if (state_.registers[18] & 8)
                    noise.lfsr = static_cast<std::uint16_t>((noise.lfsr & ~0x40U) | (feedback << 6));
            }
        }
    }
    if (state_.sample_rate == 0) return;
    state_.sample_phase += state_.sample_rate;
    if (state_.sample_phase >= clock_rate) {
        state_.sample_phase -= clock_rate;
        enqueue_sample();
    }
}

std::array<std::uint8_t, 4> Apu::channel_levels() const {
    std::array<std::uint8_t, 4> levels{};
    if (!state_.powered) return levels;
    for (unsigned channel = 0; channel < 2; ++channel) {
        const auto &pulse = state_.pulse[channel];
        const auto duty = state_.registers[channel == 0 ? 1 : 6] >> 6;
        if (pulse.enabled && pulse.duty_started && ((duty_patterns[duty] >> (7 - pulse.position)) & 1))
            levels[channel] = pulse.envelope.volume;
    }
    const unsigned volume = (state_.registers[12] >> 5) & 3;
    if (state_.wave.enabled && volume != 0)
        levels[2] = static_cast<std::uint8_t>(state_.wave.sample >> (volume - 1));
    if (state_.noise.enabled && !(state_.noise.lfsr & 1)) levels[3] = state_.noise.envelope.volume;
    return levels;
}

std::array<std::int16_t, 2> Apu::mixed_sample() const {
    std::array<std::int16_t, 2> mixed{};
    if (!state_.powered) return mixed;
    const auto levels = channel_levels();
    for (unsigned side = 0; side < 2; ++side) {
        int sum = 0;
        const unsigned shift = side == 0 ? 4 : 0;
        for (unsigned channel = 0; channel < 4; ++channel)
            if (dac_enabled(channel) && (state_.registers[21] & (1U << (channel + shift))))
                sum += 15 - 2 * levels[channel];
        const int gain = static_cast<int>((state_.registers[20] >> shift) & 7) + 1;
        mixed[side] = static_cast<std::int16_t>(sum * gain * 64);
    }
    return mixed;
}

void Apu::set_sample_rate(std::uint32_t rate) {
    state_.sample_rate = std::min(rate, clock_rate);
    state_.sample_phase = 0;
    state_.sample_read = state_.sample_write = state_.sample_count = 0;
    state_.highpass_capacitor.fill(0);
    state_.highpass_coefficient = rate ? static_cast<std::uint32_t>(
        std::pow(0.999958, static_cast<double>(clock_rate) / state_.sample_rate) * 65536.0) : 0;
}

void Apu::enqueue_sample() {
    const auto mixed = mixed_sample();
    const bool connected = state_.powered &&
        (dac_enabled(0) || dac_enabled(1) || dac_enabled(2) || dac_enabled(3));
    if (state_.sample_count == ApuState::audio_frames) {
        state_.sample_read = static_cast<std::uint16_t>((state_.sample_read + 1) % ApuState::audio_frames);
        --state_.sample_count;
    }
    for (unsigned side = 0; side < 2; ++side) {
        if (!connected) {
            state_.samples[state_.sample_write * 2U + side] = 0;
            continue;
        }
        const std::int64_t input = static_cast<std::int64_t>(mixed[side]) * 65536;
        const auto output = input - state_.highpass_capacitor[side];
        state_.highpass_capacitor[side] = input - (output * state_.highpass_coefficient) / 65536;
        state_.samples[state_.sample_write * 2U + side] = static_cast<std::int16_t>(
            std::clamp<std::int64_t>(output / 65536, -32768, 32767));
    }
    state_.sample_write = static_cast<std::uint16_t>((state_.sample_write + 1) % ApuState::audio_frames);
    ++state_.sample_count;
}

std::size_t Apu::drain_samples(std::span<std::int16_t> destination) {
    const auto frames = std::min<std::size_t>(destination.size() / 2, state_.sample_count);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        destination[frame * 2] = state_.samples[state_.sample_read * 2U];
        destination[frame * 2 + 1] = state_.samples[state_.sample_read * 2U + 1];
        state_.sample_read = static_cast<std::uint16_t>((state_.sample_read + 1) % ApuState::audio_frames);
    }
    state_.sample_count = static_cast<std::uint16_t>(state_.sample_count - frames);
    return frames * 2;
}
} // namespace dmg
