#pragma once
#ifdef ENABLE_AUTOPSY
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

namespace dmg {
class Cpu;
class Bus;
class Apu;
enum class AccessKind : unsigned { Read, Write, Execute };
struct HeatCell { std::array<std::uint32_t, 3> counts{}; };
struct InstructionRecord {
    std::uint64_t cycles{};
    std::uint16_t pc{}, sp{};
    std::array<std::uint8_t, 8> registers{};
    std::array<char, 96> text{};
};
struct AutopsyFrame {
    static constexpr unsigned trace_length = 128, wave_length = 512;
    std::uint64_t cycles{}, frames{}, instructions{};
    std::uint16_t pc{}, sp{};
    std::array<std::uint8_t, 8> registers{};
    std::uint8_t ie{}, interrupt_flags{}, ly{}, lcdc{}, bgp{}, obp0{}, obp1{};
    bool ime{}, halted{}, stopped{}, dma{}, vram_locked{}, oam_locked{};
    unsigned dot{}, mode{}, reported_mode{}, output_x{}, fifo_depth{}, fetch_phase{}, fetch_ticks{};
    unsigned tile_x{}, object_phase{}, previsible_discard{}, fine_discard{};
    std::uint8_t tile{}, tile_low{}, tile_high{};
    bool window{}, object_fetching{};
    std::array<std::uint8_t, 8> background_fifo{}, object_fifo{}, object_flags{};
    std::array<std::uint16_t, 8> background_tile_ids{}, object_tile_ids{};
    std::array<std::uint8_t, 4> levels{};
    std::array<std::uint8_t, 160 * 144> lcd{};
    std::array<InstructionRecord, trace_length> trace{};
    unsigned trace_count{};
    std::array<std::array<std::uint8_t, wave_length>, 4> waveforms{};
    unsigned wave_count{};
};

// Entire type and every caller are compiled out of release/Gym builds.
class Autopsy {
  public:
    void access(AccessKind kind, std::uint16_t address);
    void instruction(const Cpu &cpu, const Bus &bus);
    void frame(const Cpu &cpu, const Bus &bus);
    void audio_tick(const Apu &apu, std::uint64_t cycles);
    void capture(const Cpu &cpu, const Bus &bus);
    [[nodiscard]] AutopsyFrame snapshot() const;
    void heatmap(std::array<HeatCell, 65536> &destination) const;
    [[nodiscard]] std::uint32_t count(AccessKind kind, std::uint16_t address) const;
    [[nodiscard]] static std::array<std::uint8_t, 3> heat_rgb(const HeatCell &cell);
    void decay();

  private:
    struct AtomicCell { std::array<std::atomic<std::uint32_t>, 3> counts{}; };
    std::array<AtomicCell, 65536> heat_{};
    mutable std::mutex mutex_;
    AutopsyFrame captured_{};
    std::array<InstructionRecord, AutopsyFrame::trace_length> trace_{};
    unsigned trace_head_{}, trace_count_{};
    std::array<std::array<std::uint8_t, 3>, AutopsyFrame::trace_length> trace_bytes_{};
    std::array<std::array<std::uint8_t, AutopsyFrame::wave_length>, 4> waves_{};
    unsigned wave_head_{}, wave_count_{};
};
} // namespace dmg
#endif
