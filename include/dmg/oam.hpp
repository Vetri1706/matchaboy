#pragma once
namespace dmg {
class Ppu;
// IDU denotes an address-bus drive by the CPU's 16-bit increment/decrement unit.
enum class OamAccess { Read, Write, ReadIdu };
void corrupt_oam(Ppu &ppu, OamAccess access);
} // namespace dmg
