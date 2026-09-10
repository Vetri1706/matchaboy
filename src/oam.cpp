#include "dmg/oam.hpp"
#include "dmg/ppu.hpp"
#include <algorithm>
#include <cstdint>
namespace dmg {
// DMG's 16-bit OAM bus is shared with the CPU address increment/decrement unit.
// Patterns documented from hardware in Pan Docs, OAM_Corruption_Bug.md. The CPU
// address and written byte do not select the damaged row; the OAM scanner does.
void corrupt_oam(Ppu &ppu, OamAccess access) {
    if ((ppu.read(0xFF40) & 0x80U) == 0 || ppu.mode() != 2 || ppu.dot() >= 80)
        return;
    const unsigned row = ppu.dot() / 4U;
    if (row == 0)
        return;
    const unsigned base = row * 8U;
    const auto word = [&](unsigned offset) {
        return static_cast<std::uint16_t>(ppu.oam[offset] |
                                          (static_cast<unsigned>(ppu.oam[offset + 1]) << 8U));
    };
    const auto store_word = [&](unsigned offset, std::uint16_t value) {
        ppu.oam[offset] = static_cast<std::uint8_t>(value);
        ppu.oam[offset + 1] = static_cast<std::uint8_t>(value >> 8U);
    };
    if (access == OamAccess::ReadIdu && row >= 4 && row != 19) {
        const unsigned a = word(base - 16U), b = word(base - 8U), c = word(base),
                       d = word(base - 4U);
        store_word(base - 8U, static_cast<std::uint16_t>((b & (a | c | d)) | (a & c & d)));
        for (unsigned i = 0; i < 8; ++i) {
            const auto value = ppu.oam[base - 8U + i];
            ppu.oam[base + i] = value;
            ppu.oam[base - 16U + i] = value;
        }
    }
    const unsigned a = word(base), b = word(base - 8U), c = word(base - 4U);
    const unsigned first = access == OamAccess::Write ? ((a ^ c) & (b ^ c)) ^ c : b | (a & c);
    store_word(base, static_cast<std::uint16_t>(first));
    for (unsigned i = 2; i < 8; ++i)
        ppu.oam[base + i] = ppu.oam[base - 8U + i];
}
} // namespace dmg
