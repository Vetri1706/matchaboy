# Verification evidence

The frozen 2026-09-10 campaign passed **all ten requested Mooneye tests and all
65 selected DMG acceptance ROMs**, **20/20 Blargg ROMs**, the original OAM
aggregate's **eight modules**, and every pixel of **dmg-acid2 and all 24 DMG
Mealybug images**. Optimized and ASan/UBSan checks each passed **9,408,187
assertions**. No original ROM, reference image or test timing constant changed.

## Exact build and results

[Complete campaign](artifacts/fifo-verification-6/summary.json) — all ten commands
exited zero; all source, executable, archive and oracle integrity gates passed.
The campaign ran for 41.38 seconds on macOS arm64 with Apple Clang 21.0.0,
C++20, `-O3 -Wall -Wextra -Werror -Wpedantic`. The emulator uses the C++ standard
library only; verification tools use Python's standard library.

| Gate | Verified result | Evidence |
| --- | --- | --- |
| Strict rebuild | Passed | [Build log](artifacts/fifo-verification-6/build.log) |
| Optimized units | 22 groups, 9,407,404 assertions; 365 OAM; 418 PPU | [Unit log](artifacts/fifo-verification-6/unit.log) |
| ASan / UBSan | Same 9,408,187 assertions; no reported violations | [Sanitizer log](artifacts/fifo-verification-6/sanitizer.log) |
| Output failure handling | 4 real operating-system file-limit cases | [I/O results](artifacts/fifo-verification-6/harness-io/summary.json) |
| Mooneye | 65/65 selected DMG acceptance ROMs | [Results](artifacts/fifo-verification-6/mooneye/summary.json) |
| Blargg | 20/20; CPU aggregate includes all eleven modules | [Results](artifacts/fifo-verification-6/blargg/summary.json) |
| OAM | Seven standalones plus aggregate; all eight aggregate modules | [Results](artifacts/fifo-verification-6/oam/summary.json) |
| dmg-acid2 | 0 / 23,040 differing pixels, after 120 frames | [Comparison](artifacts/fifo-verification-6/acid2/summary.json) |
| Mealybug | 24/24 original DMG images; 0 / 552,960 differing pixels | [Comparisons](artifacts/fifo-verification-6/mealybug/summary.json) |

The [exact source archive](artifacts/fifo-verification-6/tested-source.tar.gz)
contains all 28 inventoried build, source, test and verification files.
The [preserved executable](artifacts/fifo-verification-6/dmg) is byte-identical
to the executable used by every final ROM runner.
An [independent read-only audit](artifacts/fifo-verification-6/INDEPENDENT_AUDIT.md)
rechecked all source/archive hashes, 839 oracle files, pixel comparisons,
signatures and all 118 trace endings without finding a discrepancy.

- Executable SHA-256: `d0715204b351b90814d1176856758656f6fd9509a193bed92b70c928d2e8baed`
- Source archive SHA-256: `2c20a9756e5a6e5d3be63622c1846d7c0dcb6830e2c7dd5f8407603233f040dd`

## Requested Mooneye cases

All of these original binaries passed: `timer/div_write`, `timer/tim00`,
`timer/tim01`, `timer/tim10`, `timer/tim11`, `timer/tima_reload`,
`ppu/stat_irq_blocking`, `ppu/intr_2_0_timing`, `oam_dma_restart`, and
`bits/mem_oam`, under the archive's `acceptance/` directory.
The DMA restart binary's original path is `acceptance/oam_dma_restart.gb`.

Success requires an actually retired `LD B,B` with B/C/D/E/H/L equal to
3/5/8/13/21/34. PC=0000 is not the author's protocol. The 65-ROM set contains
all 62 DMG runtime acceptance tests plus three original DMG post-boot register,
divider and hardware-I/O tests. Nine other boot ROMs target different hardware.
One DMG serial boot-phase alignment test remains excluded and failed in its
[retained probe](artifacts/paru-irq-audit/boot-oracle-audit/serial_boot_sclk_align-dmgABCmgb/run.json).
It is not counted as a pass. The harness initializes a verified post-boot state;
it does not execute a Nintendo boot ROM.

Mooneye release: `mts-20260714-0944-31510e1`, source revision
`31510e12eea6286d36eea060a6adde755e1067aa`. Archive SHA-256:
`18aa29462dfe1fcd32a2cb3621733abdc72410074918b9245aa99a6920f7d3f2`.
[MOONEYE.md](MOONEYE.md) records acquisition, circuitry and protocol details.

## What changed and how it was checked

Pixels pass through actual eight-slot background and object FIFOs, clocked
fetch phases, individual fine-scroll and offscreen discards, window restarts
and object stalls. There is no full-line rendering, framebuffer substitution,
ROM detection or opcode-result substitution in the core. CPU memory accesses
drive the same PPU, timer, serial and DMA objects used by the ROMs.

Repairs include real instruction prefetch and IRQ arbitration, source-dependent
DMA bus ownership and restart, read/write/IDU OAM corruption, separate LY,
comparison and mode edges, shared STAT OR-line blocking, STAT write glitches,
and the common PARU edge for VBlank and its STAT source.

The expanded image audit fixed mid-scanline palette, LCDC and scroll sampling,
independent bitplane reads, object fetch cancellation, the fetch counter's
terminal hold, and window retriggering. The last defect required seven genuine
offscreen window shifts at WX=0 and consistent pop-before-load FIFO ordering.
[Architectural findings](artifacts/FINAL_PIPELINE_FINDINGS.md) preserve the
trace, hardware references and rejected hypotheses.

Units cover exhaustive ALU and DAA cases, actual prefetch/HALT/interrupt flows,
timer reload races, banking and RTC boundaries, OAM bus patterns, per-dot layers,
patterned offscreen window pixels, CPU register-write collisions and two million
deterministically randomized PPU/MMIO dots. The unchanged external binaries and
images supply independent evidence beyond those implementation tests.

The original Mealybug PNGs are author-published emulator references accompanied
by physical-device photos. They are not described as direct digital hardware
captures. The harness reports only `captured` at its software breakpoint;
the verifier separately requires zero differing pixels. [MEALYBUG.md](MEALYBUG.md)
contains source, archive and reference provenance.

## Retained failures and scope

The original Mooneye baseline passed 35/62 runtime tests. The earlier
[passing milestone](artifacts/fifo-verification-2/summary.json) passed the named
suite and acid2 but did not include the additional Mealybug image audit.
Its preserved engine initially matched [1/24 Mealybug images](artifacts/mealybug-baseline/summary.json).
Intermediate campaigns remain failed in their reports; none was relabeled.

The first combined attempt exhausted disk space and remains incomplete.
Older completed traces were losslessly compressed; the
[compression inventory](artifacts/compressed-traces.json) records both original
and compressed hashes. Historical command records keep their original filenames;
append `.gz` where listed in that inventory. The final campaign's 118 traces
remain directly available.

Standalone OAM module 7 overflows its own verbose log into executable WRAM and
remains inconclusive. The unchanged aggregate executes that module and reports
`07:ok`. [OAM_ORACLE.md](OAM_ORACLE.md) retains the exact diagnosis; no timeout
counts as a pass.

This campaign verifies the requested FIFO, bus and Mooneye behavior, with the
listed additional image coverage. It does not establish universal hardware
equivalence. Some sub-dot sampling placements are explicitly inferred from
primary circuit ordering and original tests rather than analogue measurements.
The published universal 289-dot Mode 3 maximum is disputed by its own additive
penalties; no pixel-dropping clamp was added.

STOP oscillator startup, independent/offline RTC time, battery persistence,
APU, CGB hardware, boot-ROM execution and the serial boot-phase case remain
outside the completed PPU campaign. The earlier CPU-only report is preserved
in [VERIFICATION_BLARGG_BASELINE.md](VERIFICATION_BLARGG_BASELINE.md).

## Reproduce

```sh
cd dmg
python3 tools/fetch_roms.py
python3 tools/fetch_ppu_tests.py
python3 tools/fetch_mooneye.py
python3 tools/fetch_mealybug.py
python3 tools/verify_all.py --output artifacts/new-complete-run
```

Use a fresh output directory. The verifier archives the exact source before
rebuilding, preserves the resulting executable, then rejects source, binary or
oracle changes during the campaign. Missing output, timeouts, failed writes and
failed builds return nonzero status.
