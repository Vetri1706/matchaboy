#include "dmg/cpu.hpp"
#include "dmg/mmu.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::uint64_t assertions = 0;
void check(bool ok, const char *message) {
    ++assertions;
    if (!ok)
        throw std::runtime_error(message);
}
std::vector<std::uint8_t> rom(unsigned banks = 2, std::uint8_t type = 0, std::uint8_t ram = 0) {
    std::vector<std::uint8_t> data(banks * 16384U);
    for (unsigned bank = 0; bank < banks; ++bank)
        std::fill(data.begin() + bank * 16384U, data.begin() + (bank + 1) * 16384U,
                  static_cast<std::uint8_t>(bank));
    data[0x147] = type;
    data[0x149] = ram;
    unsigned code = 0;
    for (unsigned n = banks; n > 2; n >>= 1)
        ++code;
    data[0x148] = static_cast<std::uint8_t>(code);
    return data;
}
void prepare(dmg::Bus &bus, dmg::Cpu &cpu, std::initializer_list<std::uint8_t> code) {
    bus.poke(0xFF40, 0);
    bus.poke(0xFFFF, 0);
    bus.poke(0xFF0F, 0);
    cpu.pc = 0xC000;
    cpu.sp = 0xD000;
    cpu.h = 0xC1;
    cpu.l = 0;
    cpu.b = 0xC2;
    cpu.c = 0;
    cpu.d = 0xC3;
    cpu.e = 0;
    cpu.a = 0;
    cpu.f = 0;
    cpu.ime = false;
    cpu.halted = false;
    cpu.stopped = false;
    cpu.locked = false;
    std::uint16_t address = 0xC000;
    for (const auto byte : code)
        bus.poke(address++, byte);
    // Load IR through its real bus cycle before measuring instruction execution.
    cpu.prime();
}
void arithmetic() {
    dmg::Bus bus(rom());
    dmg::Cpu cpu(bus);
    for (const unsigned op : {0x80U, 0x88U, 0x90U, 0x98U, 0xB8U}) {
        prepare(bus, cpu, {static_cast<std::uint8_t>(op)});
        for (unsigned a = 0; a < 256; ++a)
            for (unsigned b = 0; b < 256; ++b)
                for (unsigned carry = 0; carry < 2; ++carry) {
                    cpu.pc = 0xC000;
                    cpu.a = static_cast<std::uint8_t>(a);
                    cpu.b = static_cast<std::uint8_t>(b);
                    cpu.f = static_cast<std::uint8_t>(carry * 16);
                    const unsigned c = (op == 0x88 || op == 0x98) ? carry : 0;
                    const bool subtract = op == 0x90 || op == 0x98 || op == 0xB8;
                    const int result =
                        subtract ? static_cast<int>(a) - static_cast<int>(b) - static_cast<int>(c)
                                 : static_cast<int>(a + b + c);
                    const auto low = static_cast<std::uint8_t>(result);
                    const unsigned half = (a ^ b ^ static_cast<unsigned>(result)) & 16U;
                    const auto flags = static_cast<std::uint8_t>(
                        (low == 0 ? 128 : 0) | (subtract ? 64 : 0) | (half ? 32 : 0) |
                        ((result < 0 || result > 255) ? 16 : 0));
                    cpu.step();
                    check(cpu.a == (op == 0xB8 ? a : low), "exhaustive arithmetic accumulator");
                    check(cpu.f == flags, "exhaustive arithmetic flags");
                }
    }
}
void daa() {
    dmg::Bus bus(rom());
    dmg::Cpu cpu(bus);
    prepare(bus, cpu, {0x27});
    for (unsigned a = 0; a < 256; ++a)
        for (unsigned flags = 0; flags < 8; ++flags) {
            const bool n = (flags & 4) != 0, h = (flags & 2) != 0, c = (flags & 1) != 0;
            const bool high = c || (!n && a > 0x99);
            const bool low = h || (!n && (a & 15) > 9);
            const int adjustment = (high ? 0x60 : 0) + (low ? 6 : 0);
            const auto expected =
                static_cast<std::uint8_t>(static_cast<int>(a) + (n ? -adjustment : adjustment));
            cpu.pc = 0xC000;
            cpu.a = static_cast<std::uint8_t>(a);
            cpu.f = static_cast<std::uint8_t>((n ? 64 : 0) | (h ? 32 : 0) | (c ? 16 : 0));
            cpu.step();
            check(cpu.a == expected, "DAA all 2048 A/N/H/C states");
            check(cpu.f == ((expected == 0 ? 128 : 0) | (n ? 64 : 0) | (high ? 16 : 0)),
                  "DAA flags and cleared H");
        }
    for (unsigned a = 0; a < 100; ++a)
        for (unsigned b = 0; b < 100; ++b) {
            const auto packed = [](unsigned n) {
                return static_cast<std::uint8_t>((n / 10) * 16 + n % 10);
            };
            prepare(bus, cpu, {0x80, 0x27});
            cpu.a = packed(a);
            cpu.b = packed(b);
            cpu.step();
            cpu.step();
            check(cpu.a == packed((a + b) % 100), "BCD addition decimal oracle");
            check((cpu.f & 16) != 0 ? a + b >= 100 : a + b < 100, "BCD addition carry");
            prepare(bus, cpu, {0x90, 0x27});
            cpu.a = packed(a);
            cpu.b = packed(b);
            cpu.step();
            cpu.step();
            check(cpu.a == packed((100 + a - b) % 100), "BCD subtraction decimal oracle");
            check((cpu.f & 16) != 0 ? a < b : a >= b, "BCD subtraction borrow");
        }
}
void signed_sp() {
    dmg::Bus bus(rom());
    dmg::Cpu cpu(bus);
    for (const unsigned sp :
         {0U, 1U, 7U, 15U, 16U, 127U, 128U, 255U, 256U, 4095U, 32767U, 32768U, 65520U, 65535U})
        for (unsigned raw = 0; raw < 256; ++raw)
            for (const auto op : {0xE8, 0xF8}) {
                prepare(bus, cpu, {static_cast<std::uint8_t>(op), static_cast<std::uint8_t>(raw)});
                cpu.sp = static_cast<std::uint16_t>(sp);
                cpu.f = 0xF0;
                const auto before = bus.cycles();
                cpu.step();
                const auto result = static_cast<std::uint16_t>(
                    static_cast<int>(sp) +
                    (raw < 128 ? static_cast<int>(raw) : static_cast<int>(raw) - 256));
                check((op == 0xE8 ? cpu.sp : static_cast<std::uint16_t>(cpu.h * 256 + cpu.l)) ==
                          result,
                      "signed SP result");
                check(cpu.f == (((sp & 15) + (raw & 15) > 15 ? 32 : 0) |
                                ((sp & 255) + raw > 255 ? 16 : 0)),
                      "signed SP flags from unsigned low byte");
                check(bus.cycles() - before == (op == 0xE8 ? 16 : 12),
                      "signed SP instruction timing");
            }
}
void cb_opcodes() {
    dmg::Bus bus(rom());
    dmg::Cpu cpu(bus);
    for (unsigned op = 0; op < 256; ++op) {
        prepare(bus, cpu, {0xCB, static_cast<std::uint8_t>(op)});
        bus.poke(0xC100, 0x81);
        cpu.f = 0x10;
        const auto before = bus.cycles();
        cpu.step();
        check(!cpu.locked && cpu.pc == 0xC002, "CB opcode implemented");
        check((cpu.f & 15) == 0, "CB flags reserved nibble zero");
        const unsigned expected = (op & 7) != 6 ? 8 : (op >= 0x40 && op < 0x80 ? 12 : 16);
        check(bus.cycles() - before == expected, "CB memory/register timing");
    }
}
void instruction_timing() {
    // Independent canonical base instruction M-cycle table, conditional path not taken.
    constexpr std::array<unsigned, 256> cycles = {
        1, 3, 2, 2, 1, 1, 2, 1, 5, 2, 2, 2, 1, 1, 2, 1, 1, 3, 2, 2, 1, 1, 2, 1, 3, 2, 2, 2, 1,
        1, 2, 1, 2, 3, 2, 2, 1, 1, 2, 1, 2, 2, 2, 2, 1, 1, 2, 1, 2, 3, 2, 2, 3, 3, 3, 1, 2, 2,
        2, 2, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2,
        1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1, 2, 2, 2, 2,
        2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1,
        1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1,
        2, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1, 2, 3, 3, 4, 3, 4, 2, 4, 2, 4, 3,
        2, 3, 6, 2, 4, 2, 3, 3, 0, 3, 4, 2, 4, 2, 4, 3, 0, 3, 0, 2, 4, 3, 3, 2, 0, 0, 4, 2, 4,
        4, 1, 4, 0, 0, 0, 2, 4, 3, 3, 2, 1, 0, 4, 2, 4, 3, 2, 4, 1, 0, 0, 2, 4};
    for (unsigned op = 0; op < 256; ++op) {
        dmg::Bus bus(rom());
        dmg::Cpu cpu(bus);
        prepare(bus, cpu, {static_cast<std::uint8_t>(op), 0, 0xC1});
        const unsigned condition = (op >> 3) & 3;
        cpu.f = static_cast<std::uint8_t>(condition == 0 ? 0x80 : condition == 2 ? 0x10 : 0);
        const auto before = bus.cycles();
        cpu.step();
        if (cycles[op] == 0)
            check(cpu.locked, "illegal primary opcode locks CPU");
        else if (bus.cycles() - before != cycles[op] * 4) {
            throw std::runtime_error("base opcode timing mismatch opcode=" + std::to_string(op) +
                                     " expected=" + std::to_string(cycles[op] * 4) +
                                     " actual=" + std::to_string(bus.cycles() - before));
        } else
            check(true, "base instruction timing");
    }
    for (unsigned cc = 0; cc < 4; ++cc)
        for (const unsigned base : {0x20U, 0xC0U, 0xC2U, 0xC4U}) {
            dmg::Bus bus(rom());
            dmg::Cpu cpu(bus);
            prepare(bus, cpu, {static_cast<std::uint8_t>(base + cc * 8), 0, 0xC1});
            cpu.f = static_cast<std::uint8_t>(cc == 1 ? 0x80 : cc == 3 ? 0x10 : 0);
            const auto before = bus.cycles();
            cpu.step();
            const unsigned expected = base == 0x20   ? 12
                                      : base == 0xC0 ? 20
                                      : base == 0xC2 ? 16
                                                     : 24;
            check(bus.cycles() - before == expected, "conditional taken cycle penalty");
        }
}
void interrupts() {
    dmg::Bus bus(rom());
    dmg::Cpu cpu(bus);
    prepare(bus, cpu, {0xFB, 0, 0});
    bus.poke(0xFFFF, 0x1F);
    bus.poke(0xFF0F, 0x1F);
    cpu.step();
    check(!cpu.ime && cpu.pc == 0xC001, "EI delayed one instruction");
    cpu.step();
    check(cpu.ime && cpu.pc == 0xC002, "EI enabled after following instruction");
    const auto before = bus.cycles();
    cpu.step();
    check(bus.cycles() - before == 20, "IRQ dispatch 20 T-cycles");
    check(cpu.pc == 0x40 && !cpu.ime && cpu.sp == 0xCFFE, "IRQ priority and stack");
    check(bus.peek(0xCFFE) == 2 && bus.peek(0xCFFF) == 0xC0, "IRQ return address");
    check((bus.peek(0xFF0F) & 31) == 30, "only selected IRQ acknowledged");
    prepare(bus, cpu, {0xFB, 0xF3, 0});
    cpu.step();
    cpu.step();
    cpu.step();
    check(!cpu.ime, "EI DI cancels enable");
    prepare(bus, cpu, {0xFB, 0xFB, 0});
    cpu.step();
    cpu.step();
    check(cpu.ime, "EI EI retains first enable deadline");
}
void instruction_pipeline() {
    dmg::Bus bus(rom());
    dmg::Cpu cpu(bus);
    const auto cold_cycles = bus.cycles();
    cpu.prime();
    check(bus.cycles() - cold_cycles == 4, "cold prime consumes one physical fetch cycle");
    check(cpu.instructions == 0, "cold prime does not retire an instruction");
    prepare(bus, cpu, {0x00, 0x04, 0x0C});
    cpu.b = cpu.c = 0;
    cpu.step();             // NOP's last M-cycle fetches INC B.
    bus.poke(0xC001, 0x05); // Change already fetched code to DEC B.
    bus.poke(0xC002, 0x0D); // Change not yet fetched code to DEC C.
    cpu.step();
    check(cpu.b == 1 && cpu.last_bytes[0] == 0x04, "IR retains fetched self-modified opcode");
    cpu.step();
    check(cpu.c == 0xFF && cpu.last_bytes[0] == 0x0D, "future fetch sees changed code");

    prepare(bus, cpu, {0x3E, 0x01});
    bus.poke(0xC001, 0x6A);
    cpu.step();
    check(cpu.a == 0x6A, "immediate operand read happens after opcode prefetch");
    check(cpu.last_byte_count == 2 && cpu.last_bytes[1] == 0x6A,
          "instruction diagnostics capture actual operand bus data");
    bus.poke(0xC100, 0x3C);
    cpu.pc = 0xC100;
    const auto before = bus.cycles();
    cpu.step();
    check(cpu.a == 0x6B && cpu.last_opcode_pc == 0xC100, "debug PC redirect invalidates IR");
    check(bus.cycles() - before == 8, "cold redirect charges fetch and execution separately");

    prepare(bus, cpu, {0x76, 0x04});
    cpu.b = 0;
    cpu.step();
    bus.poke(0xC001, 0x05);
    bus.poke(0xFFFF, 4);
    bus.poke(0xFF0F, 4);
    cpu.step();
    check(cpu.b == 0xFF && !cpu.halted, "HALT wake fetches code changed during sleep");
}
void halt_stop() {
    dmg::Bus bus(rom());
    dmg::Cpu cpu(bus);
    prepare(bus, cpu, {0x76, 0x3E, 0x12});
    bus.poke(0xFFFF, 1);
    bus.poke(0xFF0F, 1);
    cpu.step();
    cpu.step();
    check(cpu.a == 0x3E && cpu.pc == 0xC002, "HALT bug suppresses one opcode increment");
    prepare(bus, cpu, {0x76, 0});
    cpu.step();
    check(cpu.halted, "HALT sleeps without pending IRQ");
    const auto before = bus.cycles();
    cpu.step();
    check(bus.cycles() - before == 4, "HALT peripherals continue");
    bus.poke(0xFFFF, 4);
    bus.poke(0xFF0F, 4);
    cpu.step();
    check(!cpu.halted, "HALT wakes with IME disabled");
    prepare(bus, cpu, {0x10, 0, 0});
    cpu.step();
    check(cpu.stopped, "STOP enters stopped state");
    check(bus.peek(0xFF04) == 0, "STOP resets divider");
    const auto stopped = bus.cycles();
    cpu.step();
    check(bus.cycles() == stopped, "STOP freezes clocks");
    bus.poke(0xFF00, 0x10);
    bus.set_buttons(0x10);
    cpu.step();
    check(!cpu.stopped, "joypad wakes STOP");
}
void timer() {
    dmg::Bus bus(rom());
    bus.poke(0xFF40, 0);
    bus.poke(0xFF04, 0);
    bus.poke(0xFF07, 5);
    bus.tick(15);
    check(bus.peek(0xFF05) == 0, "TIMA before falling edge");
    bus.tick(1);
    check(bus.peek(0xFF05) == 1, "TIMA at falling edge");
    bus.poke(0xFF04, 0);
    bus.poke(0xFF05, 0);
    bus.tick(8);
    bus.poke(0xFF04, 0);
    check(bus.peek(0xFF05) == 1, "DIV reset falling-edge glitch");
    bus.tick(8);
    bus.poke(0xFF07, 0);
    check(bus.peek(0xFF05) == 2, "TAC disable falling-edge glitch");
    bus.poke(0xFF04, 0);
    bus.poke(0xFF07, 5);
    bus.poke(0xFF05, 255);
    bus.poke(0xFF06, 0x42);
    bus.poke(0xFF0F, 0);
    bus.tick(16);
    check(bus.peek(0xFF05) == 0 && !(bus.peek(0xFF0F) & 4), "overflow delayed reload");
    bus.tick(3);
    check(bus.peek(0xFF05) == 0, "three cycles overflow window");
    bus.tick(1);
    check(bus.peek(0xFF05) == 0x42 && (bus.peek(0xFF0F) & 4), "fourth cycle reload and IRQ");
    bus.poke(0xFF05, 0x77);
    check(bus.peek(0xFF05) == 0x42, "TIMA write ignored during reload");
    bus.poke(0xFF06, 0x23);
    check(bus.peek(0xFF05) == 0x23, "TMA write updates reload value");
    bus.tick(4);
    bus.poke(0xFF07, 0);
    bus.poke(0xFF04, 0);
    bus.poke(0xFF07, 5);
    bus.poke(0xFF05, 255);
    bus.poke(0xFF0F, 0);
    bus.tick(18);
    check(bus.peek(0xFF05) == 0 && !(bus.peek(0xFF0F) & 4),
          "cancellation test entered overflow window");
    bus.poke(0xFF05, 0x56);
    bus.tick(2);
    check(bus.peek(0xFF05) == 0x56 && !(bus.peek(0xFF0F) & 4), "TIMA write cancels pending reload");
}
void memory_serial_dma() {
    dmg::Bus bus(rom());
    bus.poke(0xFF40, 0);
    bus.poke(0xC123, 0x5A);
    check(bus.peek(0xE123) == 0x5A, "WRAM echo read");
    bus.poke(0xFDFF, 0xA5);
    check(bus.peek(0xDDFF) == 0xA5, "WRAM echo write");
    bus.poke(0xFEA0, 0);
    check(bus.peek(0xFEA0) == 255, "unusable memory");
    bus.poke(0xFF01, 'X');
    bus.poke(0xFF02, 0x81);
    bus.poke(0xFF0F, 0);
    bus.tick(4095);
    check(bus.serial_output.empty(), "serial takes full eight bit clocks");
    bus.tick(1);
    check(bus.serial_output == "X" && (bus.peek(0xFF0F) & 8), "serial byte output and IRQ");
    check(bus.peek(0xFF01) == 255 && !(bus.peek(0xFF02) & 128), "disconnected serial input high");
    for (unsigned n = 0; n < 160; ++n)
        bus.poke(static_cast<std::uint16_t>(0xC000 + n), static_cast<std::uint8_t>(n));
    bus.poke(0xFF46, 0xC0);
    bus.tick(16);
    check(bus.read(0xC000) == 255, "DMA blocks non-HRAM CPU reads");
    bus.write(0xFF80, 0x66);
    check(bus.read(0xFF80) == 0x66, "HRAM remains accessible during DMA");
    bus.tick(640);
    for (unsigned n = 0; n < 160; ++n)
        check(bus.ppu.oam[n] == n, "DMA copied actual memory");
}
void banking() {
    dmg::Bus bus(rom(128, 3, 3));
    check(bus.peek(0x4000) == 1, "MBC1 initial bank");
    bus.poke(0x2000, 0);
    check(bus.peek(0x4000) == 1, "MBC1 forbidden zero bank");
    bus.poke(0x2000, 2);
    bus.poke(0x4000, 3);
    check(bus.peek(0x4000) == 98, "MBC1 7-bit ROM bank");
    bus.poke(0x6000, 1);
    check(bus.peek(0) == 96, "MBC1 advanced lower bank");
    bus.poke(0, 0x0A);
    bus.poke(0xA000, 0x33);
    bus.poke(0x4000, 2);
    bus.poke(0xA000, 0x22);
    bus.poke(0x4000, 3);
    check(bus.peek(0xA000) == 0x33, "MBC1 RAM bank isolation");
    bus.poke(0, 0);
    check(bus.peek(0xA000) == 255, "disabled external RAM");
    dmg::Bus mbc3(rom(128, 0x10, 3));
    mbc3.poke(0xFF40, 0);
    mbc3.poke(0, 0x0A);
    mbc3.poke(0x2000, 127);
    check(mbc3.peek(0x4000) == 127, "MBC3 full ROM bank");
    mbc3.poke(0x4000, 8);
    mbc3.poke(0xA000, 58);
    mbc3.poke(0x6000, 0);
    mbc3.poke(0x6000, 1);
    check(mbc3.peek(0xA000) == 58, "RTC latch seconds");
    mbc3.tick(4194304);
    check(mbc3.peek(0xA000) == 58, "RTC latched value stable");
    mbc3.poke(0x6000, 0);
    mbc3.poke(0x6000, 1);
    check(mbc3.peek(0xA000) == 59, "RTC emulated second advancement");
    mbc3.poke(0x4000, 0x0C);
    mbc3.poke(0xA000, 0x40);
    mbc3.tick(4194304);
    mbc3.poke(0x6000, 0);
    mbc3.poke(0x6000, 1);
    mbc3.poke(0x4000, 8);
    check(mbc3.peek(0xA000) == 59, "RTC halt bit");
    auto large_rom = rom(512, 0x1B, 4);
    for (unsigned bank = 0; bank < 512; ++bank)
        large_rom[bank * 0x4000U + 1] = static_cast<std::uint8_t>(bank >> 8U);
    dmg::Cartridge mbc5(std::move(large_rom));
    for (unsigned bank = 0; bank < 512; ++bank) {
        mbc5.write(0x2000, static_cast<std::uint8_t>(bank));
        mbc5.write(0x3000, static_cast<std::uint8_t>(bank >> 8U));
        check(mbc5.read(0x4000) == (bank & 255U) && mbc5.read(0x4001) == (bank >> 8U),
              "MBC5 all 512 ROM banks including bank zero");
        check(mbc5.read(0) == 0 && mbc5.read(1) == 0, "MBC5 lower ROM bank stays fixed");
    }
    mbc5.write(0, 0x1A);
    for (unsigned bank = 0; bank < 16; ++bank) {
        mbc5.write(0x4000, static_cast<std::uint8_t>(bank));
        mbc5.write(0xA000, static_cast<std::uint8_t>(bank));
    }
    for (unsigned bank = 0; bank < 16; ++bank) {
        mbc5.write(0x4000, static_cast<std::uint8_t>(bank | 0xF0U));
        check(mbc5.read(0xA000) == bank, "MBC5 RAM isolation and unused bank bits");
    }
    mbc5.write(0, 0);
    mbc5.write(0xA000, 0);
    check(mbc5.read(0xA000) == 255, "MBC5 disabled RAM bus");
    mbc5.write(0, 10);
    check(mbc5.read(0xA000) == 15, "MBC5 disabled RAM ignores writes");
    dmg::Cartridge rumble(rom(2, 0x1E, 5));
    rumble.write(0, 10);
    rumble.write(0x4000, 7);
    rumble.write(0xA000, 0x57);
    rumble.write(0x4000, 15);
    check(rumble.rumble_active() && rumble.read(0xA000) == 0x57,
          "MBC5 rumble bit drives motor state rather than RAM address");
    rumble.write(0x4000, 7);
    check(!rumble.rumble_active(), "MBC5 rumble off");
}
void ppu_timing() {
    dmg::Bus bus(rom());
    bus.poke(0xFF40, 0);
    bus.poke(0xFF40, 0x91);
    bus.poke(0xFF0F, 0);
    check(bus.ppu.mode() == 0, "LCD enable first line skips OAM search");
    bus.tick(450);
    check(bus.peek(0xFF44) == 0, "LY remains zero before its startup counter edge");
    bus.tick(1);
    check(bus.peek(0xFF44) == 1 && bus.ppu.mode() == 0,
          "LY counter advances before the 452-dot physical startup line ends");
    bus.tick(1);
    check(bus.ppu.mode() == 2, "scanline starts in mode 2");
    bus.tick(79);
    check(bus.ppu.mode() == 2, "OAM search 80 dots");
    bus.tick(1);
    check(bus.ppu.mode() == 3, "pixel transfer starts at dot 80");
    bus.tick(171);
    check(bus.ppu.mode() == 3, "minimum transfer not early");
    bus.tick(1);
    check(bus.ppu.mode() == 0, "minimum transfer 172 dots");
    bus.tick(204);
    check(bus.ppu.mode() == 2 && bus.peek(0xFF44) == 2, "456-dot scanline");
    bus.tick(456 * 142);
    check(bus.ppu.mode() == 1 && bus.peek(0xFF44) == 144 && (bus.peek(0xFF0F) & 1) == 0,
          "physical VBlank begins before the PARU interrupt latch");
    bus.tick(1);
    check((bus.peek(0xFF0F) & 1) != 0, "PARU raises VBlank on the following clock");
    bus.tick(456 * 9 + 2);
    check(bus.peek(0xFF44) == 153, "LY153 first four dots");
    bus.tick(1);
    check(bus.peek(0xFF44) == 0, "LY153 dot4 quirk");
    bus.tick(452);
    check(bus.ppu.mode() == 2, "4560-dot VBlank ends");
    bus.poke(0xFF40, 0);
    bus.tick(1000);
    check(bus.peek(0xFF44) == 0 && bus.ppu.mode() == 0, "LCD off resets/freezes scanline");
}
void ppu_pixels() {
    dmg::Bus bus(rom());
    bus.poke(0xFF40, 0);
    bus.poke(0xFF47, 0xE4);
    bus.poke(0xFF48, 0xE4);
    for (unsigned row = 0; row < 8; ++row) {
        bus.ppu.vram[row * 2] = 255;
        bus.ppu.vram[16 + row * 2 + 1] = 255;
        bus.ppu.vram[32 + row * 2] = 255;
        bus.ppu.vram[32 + row * 2 + 1] = 255;
    }
    for (unsigned n = 0; n < 1024; ++n)
        bus.ppu.vram[0x1C00 + n] = 1;
    bus.poke(0xFF4A, 0);
    bus.poke(0xFF4B, 87);
    bus.ppu.oam[0] = 16;
    bus.ppu.oam[1] = 8;
    bus.ppu.oam[2] = 2;
    bus.ppu.oam[3] = 0;
    bus.poke(0xFF40, 0xF3);
    bus.tick(452 + 456);
    for (unsigned x = 0; x < 160; ++x)
        check(bus.ppu.framebuffer[160 + x] == (x < 8    ? 3
                                               : x < 80 ? 1
                                                        : 2),
              "background/window/object pixel layering");
    bus.poke(0xFF40, 0);
    bus.ppu.oam[3] = 0x80;
    bus.poke(0xFF40, 0xF3);
    bus.tick(452 + 456);
    check(bus.ppu.framebuffer[160] == 1, "object behind nonzero BG");
}
void dma_start_restart() {
    dmg::Bus bus(rom());
    bus.poke(0xFF40, 0);
    for (unsigned i = 0; i < 160; ++i) {
        bus.poke(static_cast<std::uint16_t>(0xC000 + i), 0x11);
        bus.poke(static_cast<std::uint16_t>(0xD000 + i), 0x22);
    }
    bus.ppu.oam[0] = 0x33;
    bus.poke(0xFF46, 0xC0);
    check(bus.read(0xFE00) == 0x33, "DMA first following M-cycle still permits OAM access");
    check(bus.read(0xFE00) == 0xFF, "DMA next M-cycle owns OAM bus");
    check(bus.ppu.oam[0] == 0x33, "DMA activation does not complete a byte transfer");
    bus.tick(1);
    check(bus.ppu.oam[0] == 0x11, "DMA first byte appears after startup");
    bus.poke(0xFF46, 0xD0);
    bus.tick(4);
    check(bus.ppu.oam[1] == 0x11, "DMA restart preserves old transfer during startup");
    bus.tick(5);
    check(bus.ppu.oam[0] == 0x22, "DMA restarted source replaces first byte");
    bus.tick(632);
    check(bus.dma_active(), "DMA final byte still pending before last M-cycle");
    bus.tick(4);
    check(!bus.dma_active(), "DMA completes after 160 bytes");
    for (const auto value : bus.ppu.oam)
        check(value == 0x22, "DMA restarted transfer completes entire OAM");
}
void rtc_rollover() {
    dmg::Bus bus(rom(2, 0x10, 3));
    bus.poke(0xFF40, 0);
    bus.poke(0, 10);
    const std::array<std::uint8_t, 5> initial{59, 59, 23, 255, 1};
    for (unsigned i = 0; i < 5; ++i) {
        bus.poke(0x4000, static_cast<std::uint8_t>(8 + i));
        bus.poke(0xA000, initial[i]);
    }
    bus.tick(4194304);
    bus.poke(0x6000, 0);
    bus.poke(0x6000, 1);
    for (unsigned i = 0; i < 5; ++i) {
        bus.poke(0x4000, static_cast<std::uint8_t>(8 + i));
        check(bus.peek(0xA000) == (i == 4 ? 0x80 : 0), "RTC 512-day rollover and sticky carry");
    }
    bus.poke(0xA000, 0);
    bus.poke(0x6000, 0);
    bus.poke(0x6000, 1);
    check(bus.peek(0xA000) == 0, "RTC carry explicitly cleared by software");
    dmg::Bus header_only(rom(2, 2, 0));
    header_only.poke(0, 10);
    header_only.poke(0xA000, 0x55);
    check(header_only.peek(0xA000) == 255, "zero declared RAM remains absent by default");
    dmg::Bus configured(rom(2, 2, 0), 8192);
    configured.poke(0, 10);
    configured.poke(0xA000, 0x55);
    check(configured.peek(0xA000) == 0x55, "explicit cartridge RAM configuration");
}
unsigned transfer_duration(dmg::Bus &bus) {
    // Measurements use a normal line after the hardware-specific short LCD
    // startup line; the separate edge suite checks startup itself.
    bus.tick(452);
    bus.tick(80);
    check(bus.ppu.mode() == 3, "timing sweep reaches mode3");
    unsigned duration = 0;
    while (bus.ppu.mode() == 3 && duration < 376) {
        bus.tick(1);
        ++duration;
    }
    check(bus.ppu.mode() == 0, "timing sweep completes all 160 pixels");
    return duration;
}
void ppu_timing_sweeps() {
    for (unsigned scx = 0; scx < 256; ++scx) {
        dmg::Bus bus(rom());
        bus.poke(0xFF40, 0);
        bus.poke(0xFF43, static_cast<std::uint8_t>(scx));
        bus.poke(0xFF40, 0x91);
        check(transfer_duration(bus) == 172 + (scx & 7),
              "all scroll phases extend mode3 by discarded pixels");
    }
    for (unsigned scx = 0; scx < 8; ++scx)
        for (unsigned x = 0; x < 168; ++x) {
            dmg::Bus bus(rom());
            bus.poke(0xFF40, 0);
            bus.poke(0xFF43, static_cast<std::uint8_t>(scx));
            bus.ppu.oam[0] = 16;
            bus.ppu.oam[1] = static_cast<std::uint8_t>(x);
            bus.poke(0xFF40, 0x93);
            const unsigned fine = (x + scx) & 7,
                           penalty = x == 0 ? 11 : 6 + (fine < 5 ? 5 - fine : 0);
            check(transfer_duration(bus) == 172 + scx + penalty,
                  "single object fetch timing for every X/fine-scroll phase");
        }
    for (unsigned wx = 7; wx < 167; ++wx) {
        dmg::Bus bus(rom());
        bus.poke(0xFF40, 0);
        bus.poke(0xFF4B, static_cast<std::uint8_t>(wx));
        bus.poke(0xFF4A, 0);
        bus.poke(0xFF40, 0xB1);
        check(transfer_duration(bus) == 178, "window restarts fetcher for six dots");
    }
}
void ppu_mmio_fuzz() {
    dmg::Bus bus(rom());
    std::mt19937 random(0x534D3833);
    constexpr std::array<std::uint16_t, 10> registers{0xFF40, 0xFF41, 0xFF42, 0xFF43, 0xFF45,
                                                      0xFF47, 0xFF48, 0xFF49, 0xFF4A, 0xFF4B};
    constexpr unsigned ticks = 2000000;
    for (unsigned t = 0; t < ticks; ++t) {
        const auto bits = random();
        if ((bits & 15U) == 0) {
            const auto address = registers[(bits >> 4U) % registers.size()];
            auto value = static_cast<std::uint8_t>(bits >> 16U);
            if (address == 0xFF40 && t < 1404480)
                value |= 0x80;
            bus.poke(address, value);
        }
        if ((bits & 63U) == 1)
            bus.poke(static_cast<std::uint16_t>(0x8000U + ((bits >> 6U) & 8191U)),
                     static_cast<std::uint8_t>(bits >> 24U));
        if ((bits & 63U) == 2)
            bus.ppu.oam[(bits >> 6U) % 160] = static_cast<std::uint8_t>(bits >> 24U);
        bus.tick(1);
        check(bus.ppu.dot() < 456, "fuzz dot counter bounded");
        check(bus.peek(0xFF44) <= 153, "fuzz LY bounded");
        check(bus.ppu.mode() <= 3, "fuzz mode bounded");
        check(bus.ppu.vram_blocked(true) ==
                  ((bus.peek(0xFF40) & 128) != 0 && (bus.peek(0xFF41) & 3) == 3),
              "fuzz write arbitration tracks reported mode");
    }
    for (const auto shade : bus.ppu.framebuffer)
        check(shade < 4, "fuzz framebuffer contains valid DMG shades");
}
void post_boot_video_memory() {
    auto program = rom();
    program[0x104] = 0xAB;
    program[0x133] = 0x41;
    dmg::Bus bus(std::move(program));
    check(bus.cycles() == 0, "post-boot state establishes an epoch, not executed boot cycles");
    check(bus.peek(0xFF00) == 0xCF, "post-boot P1 selects both released key groups");
    check(bus.peek(0xFF44) == 0 && bus.peek(0xFF41) == 0x85 && bus.ppu.dot() == 397,
          "post-boot LCD handoff is physical line153 with LY already zero");
    // Independent expected raster: A=1010 -> 11001100, B=1011 -> 11001111;
    // each row is doubled vertically and the second color plane is clear.
    constexpr std::array<std::uint8_t, 8> first{0xCC, 0, 0xCC, 0, 0xCF, 0, 0xCF, 0};
    constexpr std::array<std::uint8_t, 8> last{0x30, 0, 0x30, 0, 0x03, 0, 0x03, 0};
    for (unsigned i = 0; i < first.size(); ++i) {
        check(bus.peek(static_cast<std::uint16_t>(0x8010U + i)) == first[i],
              "logo is derived from first cartridge-header byte");
        check(bus.peek(static_cast<std::uint16_t>(0x8188U + i)) == last[i],
              "logo expands through final cartridge-header byte");
    }
    check(bus.peek(0x8190) == 0x3C && bus.peek(0x8194) == 0xB9 && bus.peek(0x819E) == 0x3C,
          "generic DMG registered mark survives boot handoff");
    for (unsigned tile = 1; tile <= 12; ++tile) {
        check(bus.peek(static_cast<std::uint16_t>(0x9903U + tile)) == tile,
              "logo top-row tile order and placement");
        check(bus.peek(static_cast<std::uint16_t>(0x9923U + tile)) == tile + 12,
              "logo bottom-row tile order and placement");
    }
    check(bus.peek(0x9910) == 25, "registered-mark tilemap placement");
    check(bus.peek(0x8000) == 0 && bus.peek(0x8191) == 0 && bus.peek(0x819F) == 0 &&
              bus.peek(0x9903) == 0 && bus.peek(0x9911) == 0 && bus.peek(0x9FFF) == 0,
          "unused video memory and high color planes remain clear");
    bus.poke(0xFF40, 0);
    bus.write(0x8190, 0);
    check(bus.read(0x8190) == 0, "boot tile is ordinary writable VRAM, not a rendering override");
}
void palette_write_bus() {
    for (const std::uint16_t address : {0xFF47, 0xFF48, 0xFF49}) {
        dmg::Bus bus(rom());
        bus.poke(0xFF40, 0);
        bus.ppu.vram.fill(0);
        bus.ppu.oam.fill(0);
        const bool object = address != 0xFF47;
        if (object) {
            for (unsigned row = 0; row < 8; ++row)
                bus.ppu.vram[row * 2] = 0xFF; // Object color 1, distinct from transparent 0.
            bus.ppu.oam[0] = 16;
            bus.ppu.oam[1] = 8;
            bus.ppu.oam[3] = address == 0xFF49 ? 0x10 : 0;
        }
        bus.poke(0xFF47, 0);
        bus.poke(address, object ? 0x04 : 0x01);
        bus.poke(0xFF40, object ? 0x93 : 0x91);
        bus.tick(452); // First ordinary line includes a real OAM search.
        unsigned guard = 0;
        while ((bus.ppu.mode() != 3 || bus.ppu.output_x() != 4) && guard++ < 456)
            bus.tick(1);
        check(guard < 456 && bus.ppu.output_x() == 4, "palette fixture reaches emitted pixel 4");
        const auto before = bus.cycles();
        bus.write(address, object ? 0x08 : 0x02);
        check(bus.cycles() - before == 4, "palette bus remains one real CPU M-cycle");
        check(bus.ppu.framebuffer[160 + 3] == 1, "palette write preserves the previous pixel");
        check(bus.ppu.framebuffer[160 + 4] == 3, "first write-bus dot exposes old OR new shade");
        check(bus.ppu.framebuffer[160 + 5] == 2 && bus.ppu.framebuffer[160 + 6] == 2 &&
                  bus.ppu.framebuffer[160 + 7] == 2,
              "subsequent write-bus dots expose the new shade");
        check(bus.peek(address) == (object ? 0x08 : 0x02), "architectural palette latch commits");
    }
}
void control_write_bus() {
    for (const bool object : {false, true}) {
        dmg::Bus bus(rom());
        bus.poke(0xFF40, 0);
        bus.ppu.vram.fill(0);
        bus.ppu.oam.fill(0);
        for (unsigned row = 0; row < 8; ++row)
            bus.ppu.vram[row * 2 + (object ? 1 : 0)] = 0xFF;
        if (object) {
            bus.ppu.oam[0] = 16;
            bus.ppu.oam[1] = 8;
        }
        bus.poke(0xFF47, object ? 0 : 0xE4);
        bus.poke(0xFF48, 0xE4);
        bus.poke(0xFF40, object ? 0x93 : 0x91);
        bus.tick(452);
        unsigned guard = 0;
        while ((bus.ppu.mode() != 3 || bus.ppu.output_x() != 4) && guard++ < 456)
            bus.tick(1);
        check(guard < 456, "LCDC fixture reaches an active pixel stream");
        const auto before = bus.cycles();
        const auto disabled = static_cast<std::uint8_t>(object ? 0x91 : 0x90);
        bus.write(0xFF40, disabled);
        check(bus.cycles() - before == 4, "LCDC write consumes one real CPU M-cycle");
        check(bus.ppu.framebuffer[163] == (object ? 2 : 1), "LCDC preserves already emitted pixel");
        check(bus.ppu.framebuffer[164] == (object ? 2 : 1), "LCDC write preserves pending pixel");
        check(bus.ppu.framebuffer[165] == 0 && bus.ppu.framebuffer[166] == 0 &&
                  bus.ppu.framebuffer[167] == 0,
              "pixel enable observes driven LCDC data before architectural latch");
        check(bus.peek(0xFF40) == disabled, "CPU-visible LCDC stores completed write");
        check(bus.ppu.mode() == 3 && bus.ppu.output_x() == 8,
              "disabling a layer preserves the running pixel pipeline");
    }
}
void scroll_write_bus() {
    for (const std::uint16_t address : {0xFF42, 0xFF43}) {
        dmg::Bus bus(rom());
        bus.poke(0xFF40, 0);
        bus.ppu.vram.fill(0);
        bus.ppu.oam.fill(0);
        for (unsigned row = 0; row < 8; ++row) {
            bus.ppu.vram[row * 2] = 0xFF;          // Tile 0: color 1.
            bus.ppu.vram[16 + row * 2 + 1] = 0xFF; // Tile 1: color 2.
        }
        bus.ppu.vram[0x1800 + (address == 0xFF42 ? 32 : 1)] = 1;
        bus.poke(0xFF47, 0xE4);
        bus.poke(0xFF40, 0x91);
        // Coincide a real scroll write with the first visible tile's address
        // capture. The address bus changes before CPU-visible T3 readback.
        bus.tick(452 + 84);
        const auto before = bus.cycles();
        bus.write(address, 8);
        check(bus.cycles() - before == 4, "scroll write consumes one CPU M-cycle");
        check(bus.peek(address) == 8, "scroll write commits architectural readback");
        unsigned guard = 0;
        while (bus.ppu.output_x() < 8 && guard++ < 456)
            bus.tick(1);
        check(guard < 456, "scroll fixture emits a complete first tile");
        for (unsigned x = 0; x < 8; ++x)
            check(bus.ppu.framebuffer[160 + x] == 2,
                  "live scroll address selects the new tile before T3");
    }
}
void initial_lcd_clock_sampling() {
    for (const std::uint16_t address : {0xFF40, 0xFF47}) {
        dmg::Bus bus(rom());
        bus.poke(0xFF40, 0);
        bus.ppu.vram.fill(0);
        bus.ppu.oam.fill(0);
        for (unsigned row = 0; row < 8; ++row)
            bus.ppu.vram[row * 2] = 0xFF;
        bus.poke(0xFF47, 0x04); // Color 1 initially maps to shade 1.
        bus.poke(0xFF40, 0x91);
        bus.tick(452 + 92);
        check(bus.ppu.output_x() == 0, "first LCD clock has not yet emitted a pixel");
        bus.write(address, address == 0xFF40 ? 0x90 : 0x08);
        check(bus.ppu.output_x() == 4, "initial clock write preserves four pixel clocks");
        for (unsigned x = 0; x < 4; ++x)
            check(bus.ppu.framebuffer[160 + x] == (address == 0xFF40 ? 0 : 2),
                  "first LCD enable edge samples settled live bus data");
    }
}
} // namespace
int main() {
    const std::vector<std::pair<const char *, std::function<void()>>> tests = {
        {"exhaustive arithmetic", arithmetic},
        {"DAA and decimal BCD", daa},
        {"signed SP", signed_sp},
        {"256 CB opcodes", cb_opcodes},
        {"base and conditional timing", instruction_timing},
        {"interrupt sequencing", interrupts},
        {"instruction prefetch pipeline", instruction_pipeline},
        {"HALT bug and STOP", halt_stop},
        {"timer edges and reload races", timer},
        {"memory, serial, DMA", memory_serial_dma},
        {"post-boot video memory", post_boot_video_memory},
        {"palette write bus", palette_write_bus},
        {"LCDC write bus", control_write_bus},
        {"scroll write bus", scroll_write_bus},
        {"initial LCD clock sampling", initial_lcd_clock_sampling},
        {"MBC1/MBC3/MBC5/RTC", banking},
        {"PPU timing and LY153", ppu_timing},
        {"PPU pixel layers", ppu_pixels},
        {"DMA startup and restart", dma_start_restart},
        {"RTC rollover and explicit RAM", rtc_rollover},
        {"PPU scroll/object/window timing sweeps", ppu_timing_sweeps},
        {"PPU 2-million-dot MMIO fuzz", ppu_mmio_fuzz}};
    unsigned failed = 0;
    for (const auto &[name, test] : tests) {
        try {
            test();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception &e) {
            ++failed;
            std::cerr << "FAIL " << name << ": " << e.what() << '\n';
        }
    }
    std::cout << tests.size() - failed << '/' << tests.size() << " groups passed; " << assertions
              << " assertions\n";
    return failed == 0 ? 0 : 1;
}
