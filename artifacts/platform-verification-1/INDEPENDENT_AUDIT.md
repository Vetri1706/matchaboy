# Independent evidence audit

Audited 2026-09-10 by the verification audit agent. **PASS for the completed 131-run ROM regression.** This audit read preserved reports, original stdout verdicts, trace tails, framebuffers, original oracle manifests, source archives, and executables. It did not rerun emulation or compilation and did not change frozen source, tools, ROMs, or reference images.

## Independently checked results

| Suite | Runs | Rechecked evidence |
| --- | ---: | --- |
| Mooneye | 65 | Every original run reports success and has BC=0305, DE=080D, HL=1522. Every final executed breakpoint is LD B,B. Exact explicit selection and all ten requested ROMs are present. |
| Blargg CPU/timing | 20 | Every original stdout contains Passed, every run exits zero, and the unchanged CPU aggregate prints 01:ok through 11:ok. |
| Blargg OAM | 8 | Seven standalone runs and the unchanged aggregate pass. The aggregate actually prints 01:ok through 08:ok, including 07:ok. |
| Blargg sound | 13 | Twelve standalone runs and the unchanged aggregate pass. The aggregate prints 01:ok through 12:ok. |
| dmg-acid2 | 1 | Run completed exactly 120 frames; independently decoded original PNG and actual PGM match all 23,040 pixels. |
| Mealybug | 24 | Each run status is captured, exit zero; independently decoded original PNGs and actual PGMs match all 552,960 pixels, zero mismatches in every image. |

The ten requested Mooneye ROMs are div_write, tim00, tim01, tim10, tim11, tima_reload, stat_irq_blocking, intr_2_0_timing, oam_dma_restart, and bits/mem_oam. The restart ROM uses its authentic upstream path `acceptance/oam_dma_restart.gb`. Success is the executed LD B,B Fibonacci protocol; PC=0000 is not a required or assumed pass condition.

All six suite summaries match the completed top-level regression summary. Raw per-run reports agree on all machine and verdict fields; the Mooneye wrapper replaces the harness's inner elapsed time with subprocess wall time. All 131 traces have complete final markers whose T-cycle count and PC/SP/A/F/BC/DE/HL match their raw reports. Their final executed trace records include PPU mode, dot, DMA, VRAM/OAM lock, FIFO depth, and fetch phase. All 89 Mooneye/Mealybug traces end at an actually executed LD B,B. These are bounded instruction-history traces, not claims of unbounded full histories.

## Integrity

- Rehashed all 57 inventoried source files; current bytes equal the recorded frozen inventory.
- Both preserved source archives have exactly the same 57 regular-file members, no duplicate names, and member bytes equal that inventory.
- Rehashed both preserved emulator copies and current `build/dmg`; all equal the recorded regression executable. All nine separately preserved platform binaries also match their recorded hashes.
- Rehashed all 839 original files in the six ROM/source/reference manifests and checked byte lengths. Every file matches. All recorded oracle manifests equal the originals; each selected ROM's recorded hash matches its original manifest.

Source archive SHA-256: `28154e7a5de8d09dc4b57001cbeb4de42e45a7c3b6753bc8f22ff01d97e8bc21`

Emulator SHA-256: `95e3b2480c541d2391be717238d27cf210d72d4170c36629e22fe790d50973ea`

## Selection and evidence boundaries

All 75 original Mooneye acceptance binaries are accounted for: 65 selected, nine excluded for other hardware targets, and `acceptance/serial/boot_sclk_align-dmgABCmgb.gb` explicitly excluded because startup serial-clock alignment is outside the verified model. Thus this is the documented 65-ROM DMG selection, not every upstream Mooneye test.

Standalone OAM module 7 is explicitly inconclusive because its original output overwrites its executable WRAM; it is not counted as passed. Its functionality is covered by the unchanged aggregate's genuine 07:ok verdict. The only cartridge RAM override is the documented 8-KiB fixture for original `halt_bug.gb`, whose cartridge type declares RAM but header size does not; its bytes remain unchanged.

The 24 Mealybug selections exactly equal original binaries with author-published DMG reference PNGs; seven CGB-only reference cases are excluded. These expected PNGs are the author's emulator references, with original physical-device photos retained for crosscheck; they are not represented as digitized hardware captures. A capture breakpoint alone is not a pass: the independently repeated pixel comparisons establish these image verdicts.

## Completed platform checks reviewed

The retained native and ASan/UBSan logs pass 22 CPU/core groups with 9,407,404 assertions, 365 OAM assertions, and 418 PPU edge assertions: 9,408,187 core assertions in total. Supplemental logs pass 36 APU checks, 298 snapshot assertions, real UDP transport checks, and 19 autopsy checks. Native, ASan/UBSan, and ThreadSanitizer Gym logs pass 80,000 SPSC jobs across four producer/consumer pairs and full-state replay for 16 machines over 20 frames. Python Gymnasium reports four tests OK; protocol rejection reports seven tests OK. Harness I/O evidence includes three actual POSIX file-size write rejections, each correctly returning failure.

The snapshot benchmark records zero measured allocations. Latency is a measured host distribution, not a hard deadline: the later native run includes save/restore maxima of 259.250/527.209 microseconds despite medians of 50.5625/59.834 microseconds. This audit does not certify an unconditional 100-microsecond bound.

No evidence-integrity or verdict discrepancy was found within this completed scope. The enclosing platform campaign's netplay and host benchmark work was still in progress when this audit was written; this document does not declare those unfinished checks, commercial-ROM verification, or the 50,000-FPS performance target achieved.
