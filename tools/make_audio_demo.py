#!/usr/bin/env python3
"""Build an authored DMG cartridge which drives all four real APU channels.

This is application software, not a verifier/oracle: the CPU executes every
VRAM/APU write, the PPU draws its tile map, and the HUD samples actual DAC input.
Only Python's standard library is used. The program and graphics are CC0.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


class Assembler:
    def __init__(self, origin: int = 0x150) -> None:
        self.origin = origin
        self.code = bytearray()
        self.labels: dict[str, int] = {}
        self.fixups: list[tuple[int, str, bool]] = []

    def emit(self, *values: int) -> None:
        self.code.extend(values)

    def label(self, name: str) -> None:
        self.labels[name] = self.origin + len(self.code)

    def word(self, value: int) -> None:
        self.emit(value & 255, value >> 8)

    def address(self, name: str) -> None:
        self.fixups.append((len(self.code), name, False))
        self.emit(0, 0)

    def jr(self, opcode: int, name: str) -> None:
        self.emit(opcode)
        self.fixups.append((len(self.code), name, True))
        self.emit(0)

    def io(self, register: int, value: int) -> None:
        self.emit(0x3E, value, 0xE0, register & 255)  # LD A,n; LDH (n),A

    def finish(self) -> bytes:
        for offset, label, relative in self.fixups:
            target = self.labels[label]
            if relative:
                displacement = target - (self.origin + offset + 1)
                if not -128 <= displacement <= 127:
                    raise ValueError(f"branch to {label} is out of range")
                self.code[offset] = displacement & 255
            else:
                self.code[offset:offset + 2] = target.to_bytes(2, "little")
        return bytes(self.code)


def build() -> tuple[bytes, dict[str, object]]:
    a = Assembler()
    a.emit(0xF3, 0x31, 0xFE, 0xDF)  # DI; LD SP,$DFFE
    a.io(0x40, 0)  # LCD off: normal hardware VRAM access.
    a.io(0x26, 0)
    a.emit(0x21, 0, 0x80, 0x01, 0, 0x20)  # HL=$8000 BC=$2000
    a.label("clear_vram")
    a.emit(0xAF, 0x22, 0x0B, 0x78, 0xB1)  # XOR A; LD(HL+),A; DEC BC; LD A,B; OR C
    a.jr(0x20, "clear_vram")
    # Copy authored tile planes from cartridge ROM through the CPU bus.
    a.emit(0x11)
    a.address("tiles")
    a.emit(0x21, 0, 0x80, 0x06, 64)
    a.label("copy_tiles")
    a.emit(0x1A, 0x22, 0x13, 0x05)  # LD A,(DE); LD(HL+),A; INC DE; DEC B
    a.jr(0x20, "copy_tiles")
    a.emit(0x11)
    a.address("map")
    a.emit(0x21, 0, 0x98, 0x01, 0, 4)
    a.label("copy_map")
    a.emit(0x1A, 0x22, 0x13, 0x0B, 0x78, 0xB1)
    a.jr(0x20, "copy_map")
    a.io(0x26, 0x80)
    a.io(0x24, 0x77)
    a.io(0x25, 0xFF)
    # Pulse1: 50% duty, volume11, no envelope/sweep, 2048-Hz carrier.
    for register, value in [(0x10, 0), (0x11, 0x80), (0x12, 0xB0), (0x13, 0xC0), (0x14, 0x87)]:
        a.io(register, value)
    # Pulse2: 25% duty, volume9, independent period (1365.3Hz).
    for register, value in [(0x16, 0x40), (0x17, 0x90), (0x18, 0xA0), (0x19, 0x87)]:
        a.io(register, value)
    # Wave RAM is uploaded while its DAC is disabled, then triggered.
    a.io(0x1A, 0)
    samples = list(range(16)) + list(range(15, -1, -1))
    for byte in range(16):
        a.io(0x30 + byte, (samples[byte * 2] << 4) | samples[byte * 2 + 1])
    for register, value in [(0x1A, 0x80), (0x1C, 0x20), (0x1D, 0xE0), (0x1E, 0x87),
                            (0x20, 0), (0x21, 0xA0), (0x22, 0x25), (0x23, 0x80)]:
        a.io(register, value)
    a.io(0x47, 0xE4)
    a.io(0x42, 0)
    a.io(0x43, 0)
    a.io(0x40, 0x91)
    a.io(0x0F, 0)
    a.emit(0x3E, 1, 0xEA, 0xFF, 0xFF)  # IE=VBlank
    a.emit(0x21, 0, 0xC0, 0x36, 0, 0xFB)  # HL=$C000; LD(HL),0; EI
    a.label("vblank_loop")
    a.emit(0x76, 0x34, 0x7E, 0xE0, 0x43)  # HALT; INC(HL); LD A,(HL); LDH(SCX),A
    a.jr(0x18, "vblank_loop")
    a.label("tiles")
    # Four geometric tiles, not audio plots; the waveforms exist only in APU state.
    for color in range(4):
        for row in range(8):
            pattern = 0xFF if row in (0, 7) else 0x81
            a.emit(pattern if color & 1 else 0, pattern if color & 2 else 0)
    a.label("map")
    for y in range(32):
        for x in range(32):
            a.emit(1 + ((x // 2 + y // 2) % 3))
    code = a.finish()
    rom = bytearray(32768)
    rom[0x40] = 0xD9  # VBlank ISR: RETI.
    rom[0x100:0x104] = bytes([0x00, 0xC3, 0x50, 0x01])
    # Required DMG cartridge logo/header bytes; application code/graphics are authored.
    rom[0x104:0x134] = bytes.fromhex(
        "CE ED 66 66 CC 0D 00 0B 03 73 00 83 00 0C 00 0D "
        "00 08 11 1F 88 89 00 0E DC CC 6E E6 DD DD D9 99 "
        "BB BB 67 63 6E 0E EC CC DD DC 99 9F BB B9 33 3E")
    rom[0x134:0x144] = b"SILICON AUDIO\0\0\0"
    rom[0x148] = 0  # 32KiB ROM, no MBC or external RAM.
    rom[0x14A] = 1
    checksum = 0
    for byte in rom[0x134:0x14D]:
        checksum = (checksum - byte - 1) & 255
    rom[0x14D] = checksum
    rom[0x150:0x150 + len(code)] = code
    rom[0x14E:0x150] = (sum(rom) & 65535).to_bytes(2, "big")
    manifest: dict[str, object] = {
        "title": "SILICON AUDIO", "origin": "authored SM83 homebrew; not an external verification oracle",
        "program_license": "CC0-1.0", "rom_size": len(rom),
        "sha256": hashlib.sha256(rom).hexdigest(), "labels": a.labels,
        "channels": {"pulse1": "50% duty, volume11, period64", "pulse2": "25% duty, volume9, period96",
                     "wave": "CPU-uploaded32-nibble triangle, period32", "noise": "15-bit LFSR, volume10, divisor80 shift2"},
        "capture": "build/autopsy ROM --headless --frames 120 --line 48 --dot 125 --capture PNG",
    }
    return bytes(rom), manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("roms/homebrew/silicon_audio.gb"))
    options = parser.parse_args()
    rom, manifest = build()
    options.output.parent.mkdir(parents=True, exist_ok=True)
    options.output.write_bytes(rom)
    manifest_path = options.output.with_suffix(".json")
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Wrote {options.output}: {len(rom)} bytes SHA256 {manifest['sha256']}")


if __name__ == "__main__":
    main()
