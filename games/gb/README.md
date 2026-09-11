# Matchaboy original Game Boy arcade

Five compact, complete Game Boy games: original game logic, lettering, tile art,
levels and synthesized sound, written by the coding assistant in Codex under the
project owner's direction. No commercial game content, external art, sound files,
font, compiler, game engine or game runtime was used. These are ordinary 32 KiB
ROM-only cartridges, with no cartridge RAM or battery-save requirement.

This directory is licensed **GPL-3.0-only**; see the repository's [LICENSE](../../LICENSE).
The same license covers the authored game code, artwork, levels and sound
sequences. ROM files retain the standard fixed Game Boy boot-header compatibility
bytes; these are not original in-game artwork or a Nintendo endorsement.

## The five games

| Game | Complete round | Controls in Matchaboy |
| --- | --- | --- |
| Pocket Racer | Pass 20 cars; three collisions end the round. | Left/Right changes lanes. |
| Moon Courier | Jump the gaps, collect a parcel, and reach the base on three routes; three falls end the round. | Left/Right walks; **Z** jumps. |
| Matcha Garden | Push the crate onto the flower in each of three solvable gardens. There is no time limit; a stuck board can be reset. | Arrows move/push; **X** resets the current board. |
| Signal Lost | Collect three beacons and reach the exit before a 90-second battery expires. | Arrows explore. |
| Orbit Guard | Shoot 15 drones; three drones crossing the defensive line end the round. | Left/Right moves; hold **Z** to fire. |

All cartridges show their own title, instructions and ending. **Enter** or **Z**
starts a round and restarts after its ending. Hardware equivalents are Start=Enter,
A=Z and B=X. Scores/progress belong to the current round and are not saved on exit.

## Build and reproduce

From the repository root, with any Python 3.10+ interpreter:

```powershell
python games/gb/build_games.py
python games/gb/build_games.py --check
```

The generator uses only Python's standard library. It contains a small SM83
assembler, explicit pixel rows for every authored tile, character lettering,
level maps, and the game code. `--check` rebuilds in memory and compares the
checked-in ROMs and manifest byte for byte. No downloaded assets or build tools
are needed. The Windows application embeds these ROMs in its library; users do
not need Python to play them.

- [Source and deterministic generator](build_games.py)
- [Machine-readable library manifest](manifest.json)
- [ROMs](roms/)
- [Full-objective input replays](test_games.py)
- [Native Windows display/speaker checks](test_windows.py)

## What the machine really does

The CPU polls the Game Boy's real active-low joypad register, `FF00`, selecting
directions and buttons separately. It derives newly pressed keys and also uses
held input where appropriate. Each normal game tick waits for the next VBlank
using the real LCD line register `FF44`. Game logic, sprite coordinates, tile
writes, collisions and sound-register writes execute as SM83 instructions.

The PPU fetches our tile planes from `8000`, a background map at `9800`, and sprite
attributes from OAM at `FE00`. Full scene changes briefly disable the LCD while
copying a map; normal play updates a few tiles and object attributes during
VBlank. The display is four shades at the native 160 by 144 resolution.

Sound is synthesized by hardware registers: pulse 1 supplies event notes, pulse
2 a quiet repeating motif, and the noise channel supplies crashes. The wave
channel is not used. No sound sample is stored in or streamed beside a cartridge.

The games use fixed levels and deterministic movement patterns. Their replay
tests are ordinary external test scripts feeding real joypad actions; there is
no trained AI, hidden solution oracle or automated gameplay agent inside a ROM.

## Inspector guide

The current GB Memory panel is an **access-activity view**, not a hex-value
editor. The following addresses describe the actual code and permit value checks
through the project's learning/test API. In the GUI, look for their reads/writes
and related activity. CPU shows the instructions generating it; Video shows the
real pixel pipeline; Audio shows actual pulse and noise output.

| Address | Use |
| --- | --- |
| `C000` | State: 0 title, 1 playing, 2 completed, 3 round lost. |
| `C001` | Eight-bit frame counter. |
| `C002` / `C003` / `C004` | Previous / current / newly pressed joypad bits. |
| `C005` / `C006` | Progress or score / remaining lives. |
| `C007` / `C008` | Player X/Y: pixels for action games, tile coordinates for grid games. |
| `C009` / `C00A` | Rival/drone position; grid games temporarily use these for a destination cell. |
| `C00E` | Moon Courier signed vertical velocity. |
| `C00F` | Orbit Guard's projectile-active flag. |
| `C010` / `C011` | Orbit Guard pulse X/Y; Moon Courier gap boundaries. |
| `C013` | Signal Lost battery seconds. |
| `C016` | Moon Courier parcel-held flag. |
| `C200`–`C5FF` | Mutable garden/maze map, 32 bytes per row. |

Garden movement examines both the destination and, for a push, the next cell.
Crates and collected maze beacons cause writes to both the WRAM board and VRAM.
Courier demonstrates signed velocity, gravity, collision and pickup state.
Racer uses lane equality plus vertical overlap. Orbit Guard tracks the player,
drone and projectile independently and compares their rectangles.

## Verification and limits

Run against an existing Matchaboy build:

```powershell
python games/gb/test_games.py --library build/smooth/libmatcha.dll --output artifacts/gb-arcade-replay
python games/gb/test_windows.py --binary build/smooth/Matchaboy.exe --output artifacts/gb-arcade-windows
```

[Full replay evidence](verification/summary.json) records **40 assertions across
all five games**, each ROM hash, input-stream hash and native library hash. The
tests start from reset, use only genuine joypad inputs, finish every game's full
objective and restart. They also produce losses by actual collisions, falls,
waiting for the battery, and drone invasions. Garden tests solve all three
boards, check wall blocking and verify B restores the whole board. **No RAM
writes or program-counter patches are used.**

[Windows evidence](verification/windows/summary.json) records each current ROM
running in the real Matchaboy app, OpenGL screenshot readback, nonzero generated
PCM, and completed Windows speaker buffers. All five checks passed with no
underruns or dropped frames during these short checks. These are device
completion/amplitude checks, not subjective listening or a long soak test.

[Native-resolution captures](verification/) include titles, normal play and
endings; [branded app captures](verification/windows/) show the actual Windows
player. Captures use only these authored games.

These are small first-edition arcade games with fixed levels and patterns.
Automated full-game replays and short Windows runs are verified. **Human
playtesting, balance review, physical Game Boy hardware, and third-party emulator
compatibility are not yet verified.** Do not claim otherwise or describe these
as ten externally reviewed commercial-quality games. This directory supplies the
five GB games; the five GBA games are maintained separately in `games/gba`.

## Attribution language

Supported by the source and verification in this directory:

> Five original Game Boy games, including their code, pixel art, levels and
> synthesized sound, created with the coding assistant in Codex under the project
> owner's direction. Automatically tested in Matchaboy; human playtesting pending.

The files do not independently authenticate a particular model name or establish
a named human copyright holder. Any “built with Astra” attribution should refer
to the actual session/model records, and distinguish assistant construction from
the owner's direction and subsequent human testing. Do not attribute Matchaboy's
pre-existing emulator engine to these game source files.
