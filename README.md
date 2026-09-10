# Matchaboy

[Download builds or build with Clang on Windows, Linux, and macOS](DOWNLOADS.md).

A C++20 Game Boy DMG emulator built from scratch, using only the C++ standard
library. The executable has no SDL, emulator-library, package-manager, or
dynamic framework dependency. The optional verification scripts use Python's
standard library to fetch external test binaries and preserve evidence.

Run commands from the cloned repository root. Downloaded test ROMs and build
products are generated locally; [ARTIFACTS.md](ARTIFACTS.md) describes the
compact verification evidence included in Git.

The [Matcha platform guide](PLATFORM.md) covers deterministic snapshots,
serial rollback over UDP, the optional native Silicon Autopsy dashboard,
the four-channel APU, and the batched C/Python Gym. It also provides the exact
commands and agent prompt for comparing this host with a faster machine.
Use `make platform` to build those optional targets.

The [current-host platform report](PLATFORM_VERIFICATION.md) records 131 passing
ROM checks, a passing 1,000-frame UDP fault run, and a valid M3 baseline of
4,408 aggregate frames/s. The 50,000-FPS and sub-millisecond rollback targets
remain unmet.

CMake and native Windows portability were added after that frozen baseline;
see [DOWNLOADS.md](DOWNLOADS.md) for current build and CI coverage.

**Previous frozen FIFO milestone:** all ten requested Mooneye tests and all 65 selected DMG
acceptance ROMs; 20 Blargg CPU/timing ROMs; all eight aggregate OAM modules;
and every pixel of dmg-acid2 and all 24 original DMG Mealybug images.
Optimized and ASan/UBSan builds each pass 9,408,187 assertions.
See [VERIFICATION.md](VERIFICATION.md) for the frozen source and executable,
original oracle hashes, exact coverage and remaining hardware scope.

The renderer uses eight-slot background and object FIFOs, per-dot fetches,
actual offscreen shifts, window and object stalls, and live register-write
sampling. [MEALYBUG.md](MEALYBUG.md) records the progression from the preserved
1/24 baseline to 24/24 exact images and explains the final timing repairs.

```sh
make -j2
make test
python3 tools/fetch_roms.py
python3 tools/fetch_ppu_tests.py
python3 tools/fetch_mooneye.py
python3 tools/fetch_mealybug.py
make verify-all
```

The build uses `-std=c++20 -O3 -Wall -Wextra -Werror -Wpedantic`. Override the
compiler with `make CXX=g++` if needed. Run a raw ROM directly:

```sh
./build/dmg roms/blargg/cpu_instrs/cpu_instrs.gb --report result.json --trace-out trace.txt
./build/dmg roms/acid2/dmg-acid2.gb --frames 120 --frame-out frame.pgm
./build/dmg roms/mooneye/acceptance/ppu/stat_irq_blocking.gb --protocol mooneye
```

No boot ROM is bundled. Execution starts at PC=0100 with DMG post-boot CPU
register values. Runtime is driven by emulated cycles, without throttling to
wall time. One T-cycle is one 4,194,304 Hz clock tick; one M-cycle is four
T-cycles. Both counts are recorded in each result.

`--ram-size BYTES` explicitly configures cartridge RAM for development ROMs
with incomplete headers. The original extra HALT test needs 8 KiB despite
declaring zero in its size field; the script records this override. Normal ROMs
use their declared cartridge configuration.

## Architecture

| Module | Responsibility |
| --- | --- |
| `cpu` | Primary/CB decoding, actual instruction prefetch, ordered bus accesses, flags, interrupt dispatch, HALT/STOP |
| `mmu` | Full address routing, access restrictions, OAM DMA, joypad, serial, peripheral clocks |
| `timer` | Internal divider, falling-edge TIMA clock, overflow/reload/write races |
| `mbc` | ROM-only, MBC1, MBC3/RTC, MBC5 ROM/RAM banking and rumble register |
| `ppu` | Per-dot OAM search, BG FIFO, window, object fetches, pixel priority, STAT and VBlank |
| `oam` | DMG read/write/IDU corruption patterns selected by the active OAM search row |
| `harness` | Binary loading, original Blargg/Mooneye verdicts, timeouts, traces, framebuffer and checked JSON output |

CPU memory transactions advance the same timer, DMA, serial and PPU instances
used during ROM execution. Unit programs also execute through this real bus.
There are no opcode-result substitutions or ROM-specific behavior in the core.
Pixels are emitted one at a time; the pipeline fetches tiles into background
and object queues, discards fine-scroll pixels, and stalls for window/object
fetches. [MOONEYE.md](MOONEYE.md) documents the fetch phases and bus timing.

## Verification contract

The runner prints bytes after actual emulated serial transfers complete. It
also recognizes Blargg's cartridge-RAM result protocol, requiring the running
signature before accepting a terminal status. Serial failure, illegal-opcode
lockup, and timeouts produce nonzero exits. A `--frames` run reports `completed`,
which is not a test pass.

Mooneye completion requires an actually retired `LD B,B` with the official
Fibonacci register signature. PC=0000 and serial text are not substitutes for
that protocol. Output write failures return an error; operating-system file
limits exercise that behavior against an original ROM.

Each verification attempt gets a fresh artifact directory. It includes every
ROM's stdout, stderr, final framebuffer, instruction trace, cycle counts, ROM
hash and source hashes. The aggregate CPU test additionally requires all eleven
numbered modules to report `ok`. A source change during a campaign invalidates
its overall result.

The Mooneye campaign includes 62 runtime tests and three verified DMG post-boot
state tests. Nine boot tests target other hardware, and one DMG serial boot-phase
case remains excluded with its failing probe recorded. The OAM runner executes
seven standalones and the aggregate, which
checks all eight modules. One original standalone overwrites its own code with
verbose output; its timeout remains inconclusive and is documented with retained
evidence in [OAM_ORACLE.md](OAM_ORACLE.md).

External binaries are downloaded unchanged. The Blargg mirror is pinned to
`c240dd7d700e5c0b00a7bbba52b53e4ee67b5f15`; the PPU test is dmg-acid2 v1.0.
The ROMs and their authors' source/readme/license files are separate from the
emulator implementation. A successful finite test suite is evidence of the
tested behavior, not a mathematical proof of every hardware edge case.

## Hardware and oracle references

- [Pan Docs hardware documentation](https://gbdev.io/pandocs/)
- [SM83 instruction reference](https://rgbds.gbdev.io/docs/v0.9.4/gbz80.7)
- [Blargg's original test ROM mirror and sources](https://github.com/retrio/gb-test-roms)
- [dmg-acid2 and published DMG reference image](https://github.com/mattcurrie/dmg-acid2)
- [Mooneye test suite and hardware observations](https://github.com/Gekkio/mooneye-test-suite)

Blargg's CPU behavior suite excludes STOP and illegal opcodes, and its timing
suite additionally excludes HALT. Separate unit tests cover these paths. The
acid2 author explicitly describes that test as rendering/priority acceptance,
not a precise mode-3 timing torture test. The Mooneye timing campaign supplies
separate external timing evidence. Finite acceptance tests do not prove every
unexercised hardware behavior; the report identifies the remaining scope limits.
