#include "dmg/cpu.hpp"
#include "dmg/mmu.hpp"
#include "dmg/oam.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
unsigned assertions = 0;
void check(bool condition, const char *message) {
    ++assertions;
    if (!condition)
        throw std::runtime_error(message);
}
void put(dmg::Ppu &ppu, unsigned address, unsigned word) {
    ppu.oam[address] = static_cast<std::uint8_t>(word);
    ppu.oam[address + 1] = static_cast<std::uint8_t>(word >> 8U);
}
unsigned get(const dmg::Ppu &ppu, unsigned address) {
    return ppu.oam[address] | (static_cast<unsigned>(ppu.oam[address + 1]) << 8U);
}
void fill(dmg::Ppu &ppu) {
    for (unsigned i = 0; i < ppu.oam.size(); ++i)
        ppu.oam[i] = static_cast<std::uint8_t>((i * 73U + 17U) ^ (i >> 1U));
}
void begin_oam_scan(dmg::Bus &bus) {
    // A CPU+Bus starts at the real post-boot handoff in VBlank. Drive the
    // LCD through its startup line to establish this test's Mode2 fixture.
    bus.poke(0xFF40, 0);
    bus.poke(0xFF40, 0x91);
    bus.tick(452);
    check(bus.ppu.mode() == 2 && bus.ppu.dot() == 0, "IDU fixture enters ordinary OAM scan");
}
void patterns() {
    for (const auto kind : {dmg::OamAccess::Write, dmg::OamAccess::Read}) {
        std::uint8_t flags = 0;
        dmg::Ppu ppu(flags);
        fill(ppu);
        for (unsigned i = 0; i < 24; ++i)
            ppu.tick();
        constexpr unsigned row = 48;
        put(ppu, row, 0x0F0F);
        put(ppu, row - 8, 0x3333);
        put(ppu, row - 4, 0x5555);
        const auto before = ppu.oam;
        dmg::corrupt_oam(ppu, kind);
        check(get(ppu, row) == (kind == dmg::OamAccess::Write ? 0x1717U : 0x3737U),
              "OAM first-word wired bus pattern");
        for (unsigned i = 0; i < 160; ++i)
            if (i >= row + 2 && i < row + 8)
                check(ppu.oam[i] == before[i - 8], "OAM preceding row tail copy");
            else if (i < row || i >= row + 8)
                check(ppu.oam[i] == before[i], "OAM unrelated words unchanged");
    }
    std::uint8_t flags = 0;
    dmg::Ppu ppu(flags);
    fill(ppu);
    for (unsigned i = 0; i < 24; ++i)
        ppu.tick();
    put(ppu, 32, 0x3333);
    put(ppu, 40, 0x0F0F);
    put(ppu, 48, 0x5555);
    put(ppu, 44, 0x7777);
    dmg::corrupt_oam(ppu, dmg::OamAccess::ReadIdu);
    check(get(ppu, 40) == 0x1717, "OAM combined read/IDU first-word pattern");
    for (unsigned i = 0; i < 8; ++i) {
        check(ppu.oam[32 + i] == ppu.oam[40 + i], "OAM combined previous-two row");
        check(ppu.oam[48 + i] == ppu.oam[40 + i], "OAM combined current row");
    }
}
void immunity() {
    for (const unsigned dots : {0U, 3U, 80U, 252U, 456U * 144U}) {
        std::uint8_t flags = 0;
        dmg::Ppu ppu(flags);
        fill(ppu);
        for (unsigned i = 0; i < dots; ++i)
            ppu.tick();
        const auto before = ppu.oam;
        for (const auto kind :
             {dmg::OamAccess::Read, dmg::OamAccess::Write, dmg::OamAccess::ReadIdu})
            dmg::corrupt_oam(ppu, kind);
        check(ppu.oam == before, "OAM row zero and non-search mode immunity");
    }
    std::uint8_t flags = 0;
    dmg::Ppu ppu(flags);
    ppu.write(0xFF40, 0);
    fill(ppu);
    const auto before = ppu.oam;
    dmg::corrupt_oam(ppu, dmg::OamAccess::Write);
    check(ppu.oam == before, "OAM disabled LCD immunity");
}
void cpu_idu() {
    struct Case {
        std::uint8_t opcode;
        std::uint16_t de;
        bool corrupt;
    };
    for (const auto item :
         {Case{0x13, 0xFE00, true}, Case{0x1B, 0xFE00, true}, Case{0x13, 0xFEFF, true},
          Case{0x1B, 0xFF00, false}, Case{0x13, 0xFDFF, false}, Case{0x1C, 0xFE00, false}}) {
        dmg::Bus bus(std::vector<std::uint8_t>(32768));
        begin_oam_scan(bus);
        dmg::Cpu cpu(bus);
        cpu.pc = 0xC000;
        cpu.d = static_cast<std::uint8_t>(item.de >> 8U);
        cpu.e = static_cast<std::uint8_t>(item.de);
        bus.poke(0xC000, item.opcode);
        fill(bus.ppu);
        bus.tick(16);
        const auto before = bus.ppu.oam;
        cpu.step();
        check((bus.ppu.oam != before) == item.corrupt, "CPU IDU pre-operation address");
    }
    for (const std::uint16_t address : {0xFE00, 0xFE9F, 0xFEA0, 0xFEFF}) {
        dmg::Bus bus(std::vector<std::uint8_t>(32768));
        begin_oam_scan(bus);
        fill(bus.ppu);
        bus.tick(16);
        const auto before = bus.ppu.oam;
        check(bus.read(address) == 0xFF, "Mode2 CPU OAM read returns FF");
        check(bus.ppu.oam != before, "Mode2 read corruption includes unusable range");
    }
}
} // namespace
int main() {
    try {
        patterns();
        immunity();
        cpu_idu();
        std::cout << "PASS " << assertions << " OAM assertions\n";
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
