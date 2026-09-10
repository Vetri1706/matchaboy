# Original OAM test oracle and standalone 7 overflow

The unchanged Blargg aggregate `oam_bug/oam_bug.gb` verifies all eight OAM modules. Its observed original cartridge-RAM log includes `01:ok` through `08:ok`, followed by `Passed`. The diagnostic run completed at 83,251,356 T-cycles using the frozen `build/dmg_long_oam` binary; evidence is in [the aggregate report](artifacts/oam-aggregate-diagnostic/summary.json).

The default `tools/verify_oam.py` run therefore executes seven standalone ROMs and that aggregate. It explicitly excludes `rom_singles/7-timing_effect.gb`, which remains **inconclusive as a standalone ROM**, for the independently observed oracle defect below. Exclusion is recorded in every summary. The aggregate still executes module 7 and checks its original expected CRC. No ROM, test expectation, or emulated write is changed.

The standalone test executes code copied from ROM bank 1 into WRAM starting at `C000`. Its original `common/shell.s` places its result log at `A004`; `write_text_out` increments a 16-bit pointer, writes a terminating zero to the next address, then writes the character to the old address. It has no bound or wrap at the cartridge-RAM limit `BFFF`.

The timing test performs 116 trials and prints a detailed 160-byte OAM dump for trials that produce corruption. During trial 17, its output exhausts the 8 KiB cartridge-RAM region:

| T-cycle before instruction | PC | Operation | Consequence |
| --- | --- | --- | --- |
|54,306,416|`C3F1`|`INC HL`, with `HL=BFFF`|Text pointer becomes `C000`.|
|54,306,424|`C3F2`|`LD (HL),00`, with `HL=C000`|The write completes at 54,306,436, replacing executable byte `C3` with `00`.|
|54,307,932|`C3FE`|`LD (HL),A`, with `HL=C000`, `A=20`|The next ASCII space overwrites that same code byte.|

The retained trace identifies the first write at line 916431 and the character write at line 916602 of `artifacts/oam-diagnose-55m/oam_bug_rom_singles_7-timing_effect.trace.txt.gz`. The trace is losslessly gzip-compressed; line numbers refer to its decompressed contents. The [compact forensic evidence](artifacts/forensics/oam-standalone-overflow.json) records addresses, timings, and hashes. The frozen binary also ran the standalone to 4 billion T-cycles without a verdict. Repeated LCD-off waits are bounded by the test's 1250-iteration fallback; extending that time limit cannot repair code already overwritten by the test itself.

This explains the timeout in this original standalone build. The aggregate's compact reporting avoids the verbose standalone log and passed all eight modules. The result establishes acceptance against those unchanged tests, not proof of every DMG bus edge case.

Run the normal set:

```sh
python3 tools/verify_oam.py
```

Reproduce the standalone forensic behavior without editing its ROM:

```sh
python3 tools/verify_oam.py --include-overflowing-single --only 7-timing_effect \
  --max-cycles 55000000 --trace-capacity 1000000
```

The forensic invocation returns nonzero on timeout. `--binary` can select a preserved executable, whose hash is recorded separately from the current source tree.

Original source: [Blargg suite pinned revision](https://github.com/retrio/gb-test-roms/tree/c240dd7d700e5c0b00a7bbba52b53e4ee67b5f15/oam_bug), locally preserved and hash-checked under `roms/blargg/oam_bug/source`. The ROM archive and source files remain unchanged.
