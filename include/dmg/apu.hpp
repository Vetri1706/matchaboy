#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace dmg {
struct EnvelopeState {
    std::uint8_t volume{}, timer{};
    bool running{};
};
struct PulseState {
    std::uint32_t timer{};
    std::uint16_t length{}, sweep_shadow{};
    std::uint8_t position{}, sweep_timer{};
    EnvelopeState envelope{};
    bool enabled{}, sweep_enabled{}, sweep_negated{}, triggered{}, duty_started{};
};
struct WaveState {
    std::uint32_t timer{};
    std::uint16_t length{};
    std::uint8_t position{}, sample{}, access_ticks{};
    bool enabled{};
};
struct NoiseState {
    std::uint32_t timer{};
    std::uint16_t length{}, lfsr{0x7FFF};
    EnvelopeState envelope{};
    bool enabled{};
};
// Fixed, pointer-free state. Snapshot codecs serialize fields, not padding.
struct ApuState {
    static constexpr std::size_t audio_frames = 4096;
    std::array<std::uint8_t, 0x17> registers{};
    std::array<std::uint8_t, 16> wave_ram{};
    std::array<PulseState, 2> pulse{};
    WaveState wave{};
    NoiseState noise{};
    std::uint64_t t_cycles{};
    std::uint32_t sample_phase{}, sample_rate{};
    std::array<std::int64_t, 2> highpass_capacitor{};
    std::uint32_t highpass_coefficient{};
    std::array<std::int16_t, audio_frames * 2> samples{};
    std::uint16_t sample_read{}, sample_write{}, sample_count{};
    std::uint8_t frame_step{};
    bool powered{};
};
static_assert(std::is_trivially_copyable_v<ApuState>);
static_assert(std::is_standard_layout_v<ApuState>);

class Apu {
  public:
    Apu() = default;
    void initialize_postboot();
    void tick();
    // Root bus calls this on DIV bit 12 falling, including a DIV reset.
    void frame_sequencer_tick();
    [[nodiscard]] std::uint8_t read(std::uint16_t address) const;
    void write(std::uint16_t address, std::uint8_t value);
    [[nodiscard]] std::array<std::uint8_t, 4> channel_levels() const;
    [[nodiscard]] std::array<std::int16_t, 2> mixed_sample() const;
    void set_sample_rate(std::uint32_t rate);
    // Interleaved stereo; return the number of int16 samples copied.
    std::size_t drain_samples(std::span<std::int16_t> destination);
    [[nodiscard]] const ApuState &state() const { return state_; }
    void restore(const ApuState &state) { state_ = state; }

  private:
    friend struct SnapshotAccess;
    ApuState state_{};
    [[nodiscard]] std::uint16_t frequency(unsigned channel) const;
    [[nodiscard]] std::uint32_t noise_period() const;
    [[nodiscard]] bool dac_enabled(unsigned channel) const;
    void trigger(unsigned channel);
    void clock_length();
    void clock_envelope(EnvelopeState &envelope, std::uint8_t control);
    void clock_sweep();
    [[nodiscard]] std::uint16_t sweep_calculate();
    void enqueue_sample();
};
} // namespace dmg
