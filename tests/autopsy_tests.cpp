#include "dmg/autopsy.hpp"
#include "dmg/cpu.hpp"
#include "dmg/mmu.hpp"
#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

namespace {
unsigned checks{};
void check(bool condition, const char *message) {
    ++checks;
    if (!condition) { std::cerr << "Autopsy failure: " << message << '\n'; std::exit(1); }
}
}
int main() {
    auto probe = std::make_unique<dmg::Autopsy>();
    {
        std::array<std::thread, 4> workers;
        for (auto &worker : workers) worker = std::thread([&probe] {
            for (unsigned i = 0; i < 10000; ++i) probe->access(dmg::AccessKind::Write, 0xCAFE);
        });
        for (auto &worker : workers) worker.join();
        check(probe->count(dmg::AccessKind::Write, 0xCAFE) == 40000, "atomic counters retain concurrent writers");
        probe->decay();
        check(probe->count(dmg::AccessKind::Write, 0xCAFE) == 37500, "per-frame decay is exact integer fifteen-sixteenths");
        auto cells = std::make_unique<std::array<dmg::HeatCell, 65536>>();
        probe->heatmap(*cells);
        check((*cells)[0xCA * 256 + 0xFE].counts[1] == 37500,
              "address high byte maps to heatmap row and low byte to column");
        check((*cells)[0xFECA].counts[1] == 0, "heatmap never transposes address bytes");
        const auto red = dmg::Autopsy::heat_rgb({{0, 10, 0}});
        const auto green = dmg::Autopsy::heat_rgb({{0, 0, 10}});
        const auto blue = dmg::Autopsy::heat_rgb({{10, 0, 0}});
        check(red[0] > red[1] && red[0] > red[2] && green[1] > green[0] &&
              green[1] > green[2] && blue[2] > blue[0] && blue[2] > blue[1],
              "writes map to red, execution to green, and reads to blue");
    }
    std::vector<std::uint8_t> rom(32768);
    // Actual SM83 instructions: LD A,$42; LD ($C000),A; LD A,($C000); JR self.
    const std::array<std::uint8_t, 10> program{0x3E,0x42,0xEA,0x00,0xC0,0xFA,0x00,0xC0,0x18,0xFE};
    std::copy(program.begin(), program.end(), rom.begin() + 0x100);
    dmg::Bus bus(std::move(rom));
    dmg::Cpu cpu(bus);
    bus.autopsy = probe.get();
    for (unsigned i = 0; i < 4; ++i) cpu.step();
    cpu.last_bytes.fill(0); // Later CPU state must not change recorded instruction bytes.
    probe->capture(cpu, bus);
    auto snapshot = probe->snapshot();
    check(probe->count(dmg::AccessKind::Write, 0xC000) == 1, "real CPU store emits write event");
    check(probe->count(dmg::AccessKind::Read, 0xC000) == 1, "real CPU load emits read event");
    check(probe->count(dmg::AccessKind::Execute, 0x100) == 1, "real IR fetch emits execute event");
    check(snapshot.registers[0] == 0x42 && snapshot.pc == cpu.pc && snapshot.cycles == bus.cycles(),
          "captured registers and time come from retired CPU state");
    check(snapshot.trace_count == 4 && snapshot.trace[0].pc == 0x100 && snapshot.trace[3].pc == 0x108,
          "scrollback contains actual retired instruction PCs");
    check(std::string(snapshot.trace[0].text.data()) == "LD A,$42" &&
          std::string(snapshot.trace[3].text.data()) == "JR $0108",
          "deferred disassembly preserves original instruction operands and relative address");
    const auto reads = probe->count(dmg::AccessKind::Read, 0xC000);
    (void)bus.peek(0xC000);
    probe->capture(cpu, bus);
    check(probe->count(dmg::AccessKind::Read, 0xC000) == reads, "inspection does not fabricate CPU bus traffic");
    bus.poke(0xFF40, 0);
    bus.poke(0x8000, 0xFF);
    bus.poke(0x8001, 0);
    bus.poke(0x9800, 0);
    bus.poke(0xFF43, 0);
    bus.poke(0xFF47, 0xE4);
    bus.poke(0xFF40, 0x91);
    unsigned clocks = 0;
    while (!(bus.ppu.mode() == 3 && bus.ppu.fifo_depth() > 0 && bus.ppu.output_x() > 0) && clocks < 1000) {
        bus.tick(1); ++clocks;
    }
    check(clocks < 1000, "real PPU reaches a populated mode-three FIFO");
    probe->capture(cpu, bus); snapshot = probe->snapshot();
    check(snapshot.mode == 3 && snapshot.fifo_depth == bus.ppu.fifo_depth() && snapshot.dot == bus.ppu.dot(),
          "inspector reads current FIFO occupancy and exact dot");
    check(snapshot.background_fifo[0] == 1 && snapshot.tile == 0 && snapshot.tile_low == 0xFF,
          "FIFO cells and fetched planes are actual tile bits");
    check(snapshot.background_tile_ids[0] == 0, "per-slot tile provenance follows actual BG FIFO load");
    check(snapshot.vram_locked && snapshot.oam_locked, "live mode-three bus locks are reported");
    bus.poke(0xFF26, 0);
    bus.poke(0xFF26, 0x80);
    bus.poke(0xFF16, 0x80);
    bus.poke(0xFF17, 0xF0);
    bus.poke(0xFF18, 0xF8);
    bus.poke(0xFF19, 0x87);
    bus.tick(2048);
    probe->capture(cpu, bus); snapshot = probe->snapshot();
    check(snapshot.levels == bus.apu.channel_levels(), "HUD amplitudes are real channel outputs");
    bool low = false, high = false;
    for (unsigned i = snapshot.wave_count > 64 ? snapshot.wave_count - 64 : 0; i < snapshot.wave_count; ++i) {
        low |= snapshot.waveforms[1][i] == 0;
        high |= snapshot.waveforms[1][i] == 15;
    }
    check(low && high, "waveform records actual pulse divider transitions");
    check(snapshot.wave_count > 0 && snapshot.levels[2] == 0 && snapshot.levels[3] == 0,
          "disabled channels have no synthetic waveform");
    std::cout << "Autopsy: " << checks << " checks passed\n";
}
