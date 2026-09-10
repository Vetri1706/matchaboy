# DMG timing verification

This campaign extends the existing headless emulator using original external
Mooneye and Blargg OAM test ROMs. No test binary, assertion, timing constant in
a test ROM, or expected framebuffer is modified. Hardware changes are shared
by every loaded cartridge; the engine does not identify test ROMs.

## External oracle

The Mooneye binaries are the author's release
[`mts-20260714-0944-31510e1`](https://gekkio.fi/files/mooneye-test-suite/mts-20260714-0944-31510e1/),
built from revision `31510e12eea6286d36eea060a6adde755e1067aa`.
The original archive SHA-256 is
`18aa29462dfe1fcd32a2cb3621733abdc72410074918b9245aa99a6920f7d3f2`.
The fetcher preserves the archive, matching source archive, licenses, symbols,
and a SHA-256 inventory for every original file.

The official success protocol is an executed `LD B,B` instruction with registers
B/C/D/E/H/L equal to 3/5/8/13/21/34. The corresponding failure signature is six
registers containing 0x42. PC=0000 is not part of the author's success protocol.
The harness checks the actual fetched opcode; debug memory reads cannot turn
a blocked CPU fetch into a success. A timeout remains inconclusive and has a
nonzero exit code. Frame-count completion never counts as a test pass.

`verify_mooneye.py` defaults to the ten requested tests, including the correct
original path `acceptance/oam_dma_restart.gb`. `--all-acceptance` runs 65 DMG
CPU A/B/C acceptance ROMs: 62 runtime tests and three original post-boot register,
divider and hardware-I/O tests. Nine boot tests target other hardware revisions;
one DMG boot serial-phase test remains excluded with its failing probe retained.
Every exclusion has a reason in the result. Execution starts after boot; no
Nintendo boot ROM is supplied or executed by this harness.

## Architecture and hardware boundaries

The CPU has an explicit instruction register. Ordinary execution uses the
opcode fetched at the preceding instruction's final fetch; it does not reread
that byte at execution time. Operand reads remain separate bus transactions.
That final fetch also latches interrupt acceptance. Interrupt entry corrects
the physical PC increment, performs the stack cycles, and fetches the vector's
first instruction. HALT has a dummy fetch and a fresh wake fetch. The initial
cold opcode fetch costs four real T-cycles; unit fixtures prime that register
before measuring individual instruction deltas. Debugger code edits must call
`Cpu::prime()` to intentionally discard a previously fetched instruction.

CPU bus transactions consume four T-cycles. The digital event schedule advances
three dots, resolves access ownership, performs the read/write or IDU bus event,
and latches IRQ input during an opcode fetch; it then advances the fourth dot.
An attempted four-dot data latch was rejected because it broke the original
Blargg instruction and memory-write timing ROMs. IRQ and data behavior are not
selected by opcode class, interrupt source, or cartridge identity.

The [primary CPU die analysis](https://iceboy.a-singer.de/doc/dmg_cpu_connections.html)
describes distinct IRQ, data and write phases, including dynamic data-bus
integration. This emulator's discrete event schedule is tested against the
listed binaries; it does not implement a transistor-level or half-dot analog
bus simulation. This boundary is explicit rather than hidden behind a claim
that finite acceptance tests prove every physical timing relationship.

Pixels are emitted individually on PPU dots. There is no full-line renderer at
a mode transition. Background fetching has Tile, Low, High and Push states.
Each address/data phase takes two dots; the terminal counter state waits until
the shifter can load, then restarts without an additional fixed sleep. Both
the background and object queues contain eight actual slots.
The background fetcher waits for the shifter to empty before parallel-loading
eight fetched pixels. The object queue merges nontransparent pixels by DMG
priority. A window collision can hold the background shifter while the object
path advances; it does not manufacture a ninth queue slot.

The first background fetch starts during the last two OAM-search dots and its
pixels undergo eight real previsible shifts before the same tile is fetched
again. This placement is an inferred digital schedule constrained by the
original images, not a measured half-dot propagation delay. Fine SCX pixels
are popped and discarded individually. Window
activation flushes and restarts the fetcher, taking six dots before pixel output
resumes for ordinary WX positions at least seven. WX=0..6 compare offscreen;
the corresponding window pixels are genuinely shifted out. Discarded and visible
pixels both retire before a waiting tile parallel-loads. Palette selection occurs when
each pixel is output. SCY and LCDC.4 are sampled separately for each tile-data
plane; coarse SCX is sampled at tile-ID fetch. Object fetches stall
the pixel output and read their tile planes on distinct dots. Mode 3 ends only
after 160 visible pixels have actually been emitted; there is no duration clamp
that silently drops pixels.

Live SCX/SCY and LCDC address bits reach the fetch circuit during CPU writes,
before stored register readback changes at T3. LCD pixel-clock activation and
already-running clock edges sample their combinational data paths separately.
The first-clock state survives window restarts and resets at transfer end.
Original Mealybug register-collision images and real CPU-bus fixtures check
these discrete phases; they are not claimed as measured analogue gate delays.

The initial LCD-enable line skips OAM search and lasts 452 dots. Ordinary lines
last 456. Internal PPU transitions, CPU-visible mode bits, comparison latching,
and read/write memory gates are represented separately. STAT sources feed one
OR line; only a low-to-high transition requests an interrupt. The temporary
all-source selection on DMG STAT writes participates in that same circuit.

The exposed LY counter updates one dot before the horizontal line-state wrap
in this event schedule (451 at startup, 455 normally). The
[pinned DMG hardware netlist](https://github.com/msinger/dmg-schematics/tree/dfbf8be3b11bbba71e4d54bfbd406150f572d9c0/netlist)
shows FF44 exposes the physical LY counter, whose clock edge differs from the
horizontal counter and LYC comparison. The original HBlank/SCX and LCD-enable
ROMs validate this separation in the emulator's dot convention. This is not
a derivation of every half-cycle or ripple-counter transient from the die.

The following PARU edge drives both the VBlank request and the Mode 1 STAT
source. It is shared circuitry rather than independent interrupt offsets.
Post-boot initialization places the PPU at physical line 153, dot 397, with
visible LY=0. The generic header-logo expansion and final tile map remain in
writable VRAM for every cartridge. The bootstrap instruction-cycle derivation
is retained under `artifacts/lcdc-pipeline-audit/boot-derivation.json` and the
three original DMG boot-state tests independently check this handoff.

DMA advances on the same dot clock. Bus contention depends on the DMA source:
video-bus DMA and main-bus DMA do not block the same CPU addresses. OAM remains
owned by DMA, while internal I/O and HRAM remain accessible. Restarting DMA has
startup latency during which the old transfer continues. PPU OAM reads observe
DMA ownership, not partially copied data through an unrestricted array access.

OAM corruption implements the documented read, write, and combined read/IDU
word patterns. The current OAM search row selects the corruption location;
the CPU's particular FE00–FEFF address does not. CPU register increments,
decrements, stack operations, HL auto-indexing and instruction/operand fetches
drive those operations. The first row is immune. Inspection through the
debugger's memory interface does not cause corruption.

MBC5 support was necessary to execute the unchanged original DMA source ROM,
whose cartridge type is 0x1B. It includes the ninth ROM address bit, switchable
bank zero, RAM banking and enable, and rumble-register address masking. There
is no cartridge-header override for Mooneye.

## Baseline and diagnostics

The [unaltered baseline run](artifacts/mooneye-baseline/summary.json) passed
35/62 DMG non-boot acceptance tests and 9/10 requested tests. The remaining named
test was DMA restart. Additional failures exposed bus ownership, LCD startup,
mode visibility, STAT comparison, and interrupt arbitration defects.

Every run records stdout, stderr, a framebuffer, a bounded instruction history,
final registers, T-cycle counts, source hashes and the executable hash. Traces
include PPU mode, LY, dot, STAT, interrupt state, DMA ownership, memory locks,
FIFO depth, fetch phase, screen X and actual executed opcode. The default
history retains the last 65,536 instruction/idle boundaries. Earlier failing
attempts are retained separately; changes during a campaign invalidate its
overall verification result.

The disassembly for retired instructions uses their actual opcode and operand
fetch bytes. `EXEC=0` entries represent interrupt/idle steps, whose lookahead
disassembly is observational and does not indicate instruction execution.

The interrupted `artifacts/fifo-verification` attempt exhausted available disk
space and is not a passed campaign. Earlier traces are now losslessly stored
as `.txt.gz`; [the compression inventory](artifacts/compressed-traces.json)
records both compressed and decompressed SHA-256 values. Every decompressed
hash was checked before removing its uncompressed copy. Historical command
records still show the filenames used at execution time; append `.gz` to find
their retained contents. Trace line numbers remain unchanged after decompression.

The harness closes and checks trace, framebuffer and JSON streams before
returning success, and checks execution-log flushes. `verify_harness_io.py`
executes an original Mooneye ROM while the operating system rejects writes via
`RLIMIT_FSIZE`; each rejected output must make the harness return an error.
Suite selection is checked explicitly, and summary files are replaced
atomically so an interrupted update cannot replace the preceding valid report
with a truncated JSON file.

## Reproduce

```sh
make -j4
make test
python3 tools/fetch_mooneye.py
python3 tools/verify_mooneye.py --all-acceptance --output artifacts/new-mooneye
python3 tools/verify_oam.py --output artifacts/new-oam
python3 tools/verify.py --extra --output artifacts/new-blargg
python3 tools/verify_ppu.py --output artifacts/new-acid2
make sanitize
```

`make verify-all` performs a fresh strict build, all unit and sanitizer checks,
and all five external campaigns, with a single top-level source/binary integrity
check. Fetch the original ROMs once before using this offline verification command.

Use fresh output paths. `--binary` permits isolated diagnostic builds in the
Mooneye, OAM and framebuffer runners. A recorded binary hash identifies what
actually ran; final verification must build and freeze the matching source tree.

## Primary hardware references

- [Mooneye original test sources and protocol](https://github.com/Gekkio/mooneye-test-suite)
- [Pixel FIFO documentation](https://gbdev.io/pandocs/pixel_fifo.html)
- [DMG OAM corruption patterns](https://gbdev.io/pandocs/OAM_Corruption_Bug.html)
- [Timer circuitry and delayed reload](https://gbdev.io/pandocs/Timer_Obscure_Behaviour.html)
- [Game Boy Complete Technical Reference](https://gekkio.fi/files/gb-docs/gbctr.pdf)
- [CPU decode and fetch/interrupt research](https://gist.github.com/SonoSooS/c0055300670d678b5ae8433e20bea595)

Finite binary tests establish the exercised behavior. They do not certify every
DMG silicon revision or untested analog and oscillator behavior. This campaign
does not add a boot ROM, APU, CGB hardware or battery-backed save persistence.

The OAM runner records an exception for an original standalone diagnostic that
overflows its own output into executable RAM. The unchanged aggregate executes
that same module; see [the retained forensic evidence](OAM_ORACLE.md). No excluded
standalone result is marked passed.
