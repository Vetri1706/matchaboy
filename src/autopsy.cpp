#include "dmg/autopsy.hpp"
#ifdef ENABLE_AUTOPSY
#include "dmg/apu.hpp"
#include "dmg/cpu.hpp"
#include "dmg/mmu.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace dmg {
std::array<std::uint8_t, 3> Autopsy::heat_rgb(const HeatCell &cell) {
    std::array<std::uint8_t, 3> rgb{};
    constexpr std::array<unsigned, 3> kind_for_rgb{1, 2, 0};
    for (unsigned channel = 0; channel < 3; ++channel)
        rgb[channel] = static_cast<std::uint8_t>(std::min(255.0,
            9.0 + std::log2(1.0 + cell.counts[kind_for_rgb[channel]]) * 38.0));
    return rgb;
}
void Autopsy::access(AccessKind kind, std::uint16_t address) {
    auto &counter = heat_[address].counts[static_cast<unsigned>(kind)];
    auto value = counter.load(std::memory_order_relaxed);
    while (value != std::numeric_limits<std::uint32_t>::max() &&
           !counter.compare_exchange_weak(value, value + 1, std::memory_order_relaxed)) {}
}
std::uint32_t Autopsy::count(AccessKind kind, std::uint16_t address) const {
    return heat_[address].counts[static_cast<unsigned>(kind)].load(std::memory_order_relaxed);
}
void Autopsy::decay() {
    for (auto &cell : heat_) for (auto &counter : cell.counts) {
        auto value = counter.load(std::memory_order_relaxed);
        while (!counter.compare_exchange_weak(value, value - ((value + 15ULL) / 16),
                                               std::memory_order_relaxed)) {}
    }
}
void Autopsy::heatmap(std::array<HeatCell, 65536> &destination) const {
    for (unsigned address = 0; address < 65536; ++address)
        for (unsigned kind = 0; kind < 3; ++kind)
            destination[address].counts[kind] = heat_[address].counts[kind].load(std::memory_order_relaxed);
}
void Autopsy::instruction(const Cpu &cpu, const Bus &bus) {
    InstructionRecord record;
    record.cycles = bus.cycles();
    record.pc = cpu.last_opcode_pc;
    record.sp = cpu.sp;
    record.registers = {cpu.a, cpu.f, cpu.b, cpu.c, cpu.d, cpu.e, cpu.h, cpu.l};
    std::lock_guard lock(mutex_);
    trace_[trace_head_] = record;
    trace_bytes_[trace_head_] = cpu.last_bytes;
    trace_head_ = (trace_head_ + 1) % AutopsyFrame::trace_length;
    trace_count_ = std::min(trace_count_ + 1, AutopsyFrame::trace_length);
}
void Autopsy::audio_tick(const Apu &apu, std::uint64_t cycles) {
    if (cycles % 32 != 0) return;
    const auto levels = apu.channel_levels();
    std::lock_guard lock(mutex_);
    for (unsigned channel = 0; channel < 4; ++channel) waves_[channel][wave_head_] = levels[channel];
    wave_head_ = (wave_head_ + 1) % AutopsyFrame::wave_length;
    wave_count_ = std::min(wave_count_ + 1, AutopsyFrame::wave_length);
}
void Autopsy::frame(const Cpu &cpu, const Bus &bus) {
    decay();
    capture(cpu, bus);
}
void Autopsy::capture(const Cpu &cpu, const Bus &bus) {
    std::lock_guard lock(mutex_);
    auto &out = captured_;
    const auto &ppu = bus.ppu;
    out.cycles = bus.cycles(); out.frames = ppu.frames(); out.instructions = cpu.instructions;
    out.pc = cpu.pc; out.sp = cpu.sp;
    out.registers = {cpu.a, cpu.f, cpu.b, cpu.c, cpu.d, cpu.e, cpu.h, cpu.l};
    out.ie = bus.ie; out.interrupt_flags = bus.iflag;
    out.ime = cpu.ime; out.halted = cpu.halted; out.stopped = cpu.stopped;
    out.dma = bus.dma_active();
    out.vram_locked = ppu.vram_blocked(); out.oam_locked = ppu.oam_blocked();
    out.ly = ppu.ly_; out.dot = ppu.dot_; out.mode = ppu.mode_; out.reported_mode = ppu.reported_mode_;
    out.output_x = ppu.screen_x_; out.fifo_depth = ppu.fifo_size_;
    out.fetch_phase = static_cast<unsigned>(ppu.fetch_stage_); out.fetch_ticks = ppu.fetch_ticks_;
    out.tile_x = ppu.fetch_tile_x_; out.tile = ppu.fetched_tile_;
    out.tile_low = ppu.fetched_low_; out.tile_high = ppu.fetched_high_;
    out.window = ppu.fetched_window_; out.object_fetching = ppu.object_fetching_;
    out.object_phase = ppu.object_phase_; out.previsible_discard = ppu.previsible_discard_;
    out.fine_discard = ppu.discard_;
    out.lcdc = ppu.lcdc_; out.bgp = ppu.bgp_; out.obp0 = ppu.obp0_; out.obp1 = ppu.obp1_;
    for (unsigned pixel = 0; pixel < 8; ++pixel) {
        out.background_fifo[pixel] = ppu.background_fifo_[(ppu.fifo_head_ + pixel) & 7];
        out.object_fifo[pixel] = ppu.object_fifo_[pixel].color;
        out.object_flags[pixel] = ppu.object_fifo_[pixel].flags;
        out.background_tile_ids[pixel] = ppu.background_tile_ids_[(ppu.fifo_head_ + pixel) & 7];
        out.object_tile_ids[pixel] = ppu.object_tile_ids_[pixel];
    }
    out.lcd = ppu.framebuffer;
    out.levels = bus.apu.channel_levels();
    out.trace_count = trace_count_;
    // Record raw executed bytes in the hot path. Only disassemble the bounded
    // visible history, not every instruction the machine executes.
    for (unsigned i = 0; i < trace_count_; ++i) {
        const auto index = (trace_head_ + AutopsyFrame::trace_length - trace_count_ + i) % AutopsyFrame::trace_length;
        out.trace[i] = trace_[index];
        const auto &bytes = trace_bytes_[index];
        const auto text = Cpu::disassemble(out.trace[i].pc, bytes[0], bytes[1], bytes[2]);
        std::copy_n(text.data(), std::min(text.size(), out.trace[i].text.size() - 1), out.trace[i].text.data());
    }
    out.wave_count = wave_count_;
    for (unsigned channel = 0; channel < 4; ++channel)
        for (unsigned i = 0; i < wave_count_; ++i)
            out.waveforms[channel][i] = waves_[channel][(wave_head_ + AutopsyFrame::wave_length - wave_count_ + i) % AutopsyFrame::wave_length];
}
AutopsyFrame Autopsy::snapshot() const {
    std::lock_guard lock(mutex_);
    return captured_;
}
} // namespace dmg
#endif
