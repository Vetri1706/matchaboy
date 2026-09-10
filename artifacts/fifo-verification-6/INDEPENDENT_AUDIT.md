# Independent read-only evidence audit

The verification reviewer independently checked this completed campaign on
2026-09-10. No evidence discrepancy was found.

- Rehashed all 28 inventoried build/source/test/tool files and all 28 members
  of the preserved source archive. Rehashed the archive itself, the preserved
  executable and the executable in `build/`; all matched the recorded hashes.
- Rehashed 839 original oracle files against their preserved inventories.
- Confirmed 65 unique Mooneye cases, including all ten requested names, with
  Fibonacci signatures in both the JSON reports and final traces.
- Recomputed the 24 Mealybug comparisons (552,960 pixels) and acid2 comparison
  (23,040 pixels): zero differences. acid2 completed exactly 120 frames.
- Confirmed all 20 Blargg and eight selected OAM runs. The CPU aggregate logs
  contain all eleven module markers; the OAM aggregate contains all eight,
  explicitly including `07:ok`.
- Checked all 118 trace final markers against their report T-cycle, PC and SP
  values, with required PPU, FIFO and bus fields. All 89 Mooneye/Mealybug capture
  traces end with an actually executed `LD B,B`.
- Confirmed optimized and ASan/UBSan logs each contain 22/22 groups and
  9,407,404 core assertions, plus 365 OAM and 418 PPU edge assertions:
  9,408,187 total per configuration.
- Confirmed all four real operating-system output-failure checks passed.

The documented extra HALT ROM's explicit 8 KiB cartridge-RAM configuration
remains visible in its command record. Original ROM bytes were unchanged.
Exclusions and finite verification limits remain as stated in `VERIFICATION.md`.

This is a reviewer audit of retained evidence, distinct from the automated
campaign and its machine-readable integrity checks in `summary.json`.
