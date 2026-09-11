# Matchaboy original GBA arcade

Five small, complete arcade games, written for this project with original game
logic, a 5x7 font, geometric pixel art, levels and PSG music/effects. Each has a
title/instructions screen, Start, a goal, a win/loss screen and Start to restart.
They are ROM programs running on the real emulated GBA CPU and peripherals.

| Game | Goal | Main controls in Matchaboy |
| --- | --- | --- |
| Drift Circuit | Finish the 3600-metre time trial in 40 seconds; avoid traffic and the verge. | Left/right steer; Z boosts; X brakes. |
| Cloud Pilot | Pass 24 gates before time runs out, preserving three shields. | Up/down fly; Z speeds up. |
| Prism Break | Break 24 bricks with three balls. Paddle edges change the bounce. | Left/right paddle; Z launches. |
| Tiny Tactics | Defeat three two-hit sentries within 40 turns, starting with nine health. | Arrows move one tile; Z attacks within two Manhattan-distance tiles; X changes target. |
| Parcel Dash | Pick up at POST and deliver to five gold-marked addresses in 60 seconds. | Arrows move; Z picks up/drops; X walks slowly. |

Enter is Start/restart and Shift is Select/back to the title in every game. The
ROM title screens label the GBA buttons: keyboard Z is A and X is B. Timing uses
the approximately 59.73 Hz GBA hardware frame clock; displayed seconds count
60 game updates. Use Matchaboy's Space key to pause.

## Source and authorship

The assistant generated the original source, glyphs, shapes, level arrangements,
short musical motifs, build script and tests during this task, under the user's
direction and with automated verification and iteration. This records the
development process; it does not independently attest a model identity or claim
human playtesting. No external game assets, game code, sound samples, fonts, game
SDK, or runtime library were downloaded or incorporated into these cartridges.
LLVM is a build tool. Matchaboy's separately credited mGBA component emulates the
GBA; it is not code linked into these game ROMs.

All original files in this directory, including the generated `.gba` ROMs, are
licensed **GPL-3.0-only** under the repository's [LICENSE](../../LICENSE).
The complete corresponding source and build instructions are here. Distribute
the source and license along with the ROM collection. The development record
does not establish a new copyright-holder name beyond the project's notices.

- [Game rules](src/game.c): five distinct game state machines and mechanics.
- [Original graphics and font](src/draw.c): GBA Mode 4, 240x160, 16 used palette
  colours, double-buffered video, DMA3 rectangle fills and packed glyph writes.
- [Entry and original music](src/main.c): hardware keypad reads, VBlank pacing,
  square-channel motifs/effects; compact ARM code runs from fast IWRAM.
- [Startup](src/start.s) and [linker layout](src/rom.ld): freestanding ARM7TDMI,
  no operating system or SDK runtime. [Manifest](manifest.json) provides library
  descriptions, keyboard controls, learning notes and Inspector hints.

## Rebuild

Install Python 3 and LLVM with `clang`, `ld.lld`, `lld-link` and `llvm-objcopy`.
From the repository root on Windows:

```powershell
.venv/Scripts/python.exe games/gba/build.py --test
```

Use `--llvm-dir` to select another LLVM directory. Building only the ROMs needs
`clang`, `ld.lld` and `llvm-objcopy`; `--test` additionally runs a Windows native
DLL containing the exact game-rule source. No GBA SDK, asset download, Python
package or package manager is used by the ROM builder. The generated ROMs are
32 KiB each. [roms/sha256.json](roms/sha256.json) records their hashes; repeated
builds with the same LLVM version and source produce identical bytes.

## Verification

The [verification directory](verification/) records automated results for the
checked-in ROM hashes. Three complementary layers are used:

1. `tests/test_logic.py` builds the exact `game.c` as a native test DLL and checks
   input, mechanics, end conditions and restart. Some focused boundary tests
   arrange state or clear hazards; their scope is explicitly recorded.
2. `tests/test_hardware.py` uses Matchaboy's `gba_arcade_probe` target to execute
   the **unmodified ARM ROMs**, read actual EWRAM, send hardware keypad buttons,
   check game progress and generated PSG PCM, and verify loss/restart or the
   default tactical victory/restart. `tests/test_playthrough.py` additionally
   completes Drift, Cloud, Prism and Parcel using only controller input. The
   tactical route in the hardware test completes the fifth game. These are
   scripted playthroughs, not a built-in player AI or human enjoyment ratings.
3. `tools/test_arcade_gba.py` launches the actual Windows player, captures title
   and gameplay screens, checks a solid background region to catch DMA rendering
   corruption, sends Start, steps frames, checks nonzero PCM and completed
   Windows speaker-device buffers when an output device is present. It does not
   claim subjective listening, latency measurements or universal compatibility.

For example, after building Matchaboy and its probe target:

```powershell
.venv/Scripts/python.exe games/gba/tests/test_hardware.py --probe build/arcade/gba_arcade_probe.exe --output games/gba/build/hardware-check
.venv/Scripts/python.exe games/gba/tests/test_playthrough.py --probe build/arcade/gba_arcade_probe.exe --output games/gba/build/playthrough-check
.venv/Scripts/python.exe tools/test_arcade_gba.py --binary build/arcade/Matchaboy.exe --output games/gba/build/windows-check
```

## Inspector map and limits

The [Game structure](src/arcade.h) begins at EWRAM `0x02000000`; all fields below
are little-endian 32-bit integers. Inspector reads are passive.

| Address | Meaning |
| --- | --- |
| 02000000 | Game kind: Drift 0, Cloud 1, Prism 2, Tactics 3, Parcel 4. |
| 02000004 | Phase: title 0, play 1, won 2, lost 3. |
| 0200000C | Game updates elapsed in the round. |
| 02000010 | Score: traffic passed, gates, bricks, defeated sentries or deliveries. |
| 02000014 | Time remaining in game updates (Tactics uses turns). |
| 02000018 | Lives, shields or health. |
| 0200001C / 02000020 | Player x / y. |
| 0200003C | Drift distance. |
| 02000040 / 02000044 | Target index / carrying or launched-ball flag. |
| 02000048 | Tactical turns used. |
| 0200004C | 24 Prism brick flags. |
| 020000AC | Objects, six integers each: x, y, vx, vy, alive, hp. |

The GBA CPU pane shows registers and nearby raw opcodes; these games do not add
a retired-instruction trace. Video is bitmap Mode 4, not a sprite/tile engine;
the Audio pane shows mixed PCM from the real GBA PSG channels. They contain no
save cartridge or persistent high scores, multiplayer, network code or trained
AI. These are short single-course/board arcade games, not full-length campaigns.

The cartridge header deliberately leaves the manufacturer's logo area blank,
so no third-party logo artwork is bundled. **Use Matchaboy's bundled skip-BIOS
GBA path. Physical GBA boot and other emulator configurations have not been
verified; these ROMs are not claimed to boot on an unmodified physical GBA.**
