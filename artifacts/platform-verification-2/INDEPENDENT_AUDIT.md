# Independent ROM evidence audit

Audited 2026-09-10 by the verification audit agent. **PASS: the completed 131-run ROM regression reproduces the independently audited first campaign.** This bounded audit read preserved evidence and recomputed hashes; it did not run tests, compilers, emulator instances, or network actors. Only this document was written.

## Cross-campaign comparison

- All 131 raw per-case reports have identical deterministic contents to `platform-verification-1`: verdict/capture status, mechanism, register values, T/M-cycle counts, instruction/frame counts, PPU and bus state, and cartridge/serial metadata. Only elapsed wall time differs.
- All 131 original stdout logs are byte-identical, including CPU aggregate 01:ok through 11:ok, OAM aggregate 01:ok through 08:ok, and sound aggregate 01:ok through 12:ok.
- All 131 actual PGM framebuffers are byte-identical. The separately retained acid2 expected PGM is also identical. Therefore the independently decoded first-campaign comparisons are reproduced: 24 Mealybug images each have zero mismatches across 23,040 pixels, and acid2 has zero mismatches after exactly 120 completed frames.
- All 131 current trace files have complete final markers matching their raw report T-cycles and PC/SP/A/F/BC/DE/HL. The final executed records retain PPU mode, dot, DMA, VRAM/OAM lock, FIFO depth, and fetch phase. All 89 Mooneye/Mealybug capture traces end at an actually executed LD B,B.
- Exact selections are unchanged: 65 Mooneye, 20 CPU/timing, eight OAM, 13 sound, one acid2, and 24 Mealybug runs. All 65 Fibonacci signatures and all ten specifically requested Mooneye ROMs are preserved. Every suite summary agrees with the completed regression summary; all recorded regression commands exit zero.

## Source, executable, and oracle integrity

The emulator SHA-256 remains `95e3b2480c541d2391be717238d27cf210d72d4170c36629e22fe790d50973ea`, identical to the audited first campaign. Rehashed the old and new preserved regression executables, the new platform's preserved emulator, and current `build/dmg`; all match.

Both new source archives have SHA-256 `047f31b99668f0f459a29530bea822054775b3c1a27c63a9a8de42cb0618c927`. Each contains exactly 57 regular-file members with no duplicate names, and every member matches the new frozen inventory and current source bytes. Relative to the first campaign, the inventory has exactly three changed files and no additions/removals:

- `src/netplay_main.cpp`
- `tools/benchmark_host.py`
- `tools/verify_platform.py`

All recorded suite oracle manifests are unchanged. Rehashed all 839 original ROM, source, and reference files across the six manifests and checked their lengths; every file matches its recorded original hash.

## Scope

This confirms the ROM evidence, not completion of the enclosing platform campaign. Netplay and host performance checks were still running when this audit was written. No commercial-ROM, 50,000-FPS, or unconditional snapshot-latency claim is made here.

The first campaign's [independent audit](../platform-verification-1/INDEPENDENT_AUDIT.md) documents the unchanged selection limits: nine other-hardware Mooneye exclusions plus unverified boot serial-clock alignment; standalone OAM7 inconclusive while the unchanged aggregate genuinely passes module7; the original halt_bug cartridge RAM fixture; and author-published Mealybug emulator PNGs corroborated by physical-device photos rather than digitized hardware captures. These limitations remain explicit. No evidence or verdict discrepancy was found.
