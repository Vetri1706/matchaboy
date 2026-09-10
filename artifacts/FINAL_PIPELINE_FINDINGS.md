# Final pixel-pipeline repairs

This audit documents the mechanism behind the final original Mealybug image
passes. The unified production campaign is recorded separately in
`fifo-verification-6/summary.json`; a private diagnostic pass is not a substitute
for that build and its complete regression checks.

## Fetch counter and live address bus

The LAXU/MESU/NYVA fetch counter holds at its terminal state until the background
shifter loads, then restarts immediately. The previous fixed two-dot sleep after
each load delayed ordinary reads while concealing incorrect live-register
sampling. The implementation now represents the hold in the Push state and
uses the same live SCX/SCY and LCDC address circuits for ordinary and initial
window fetches. There is no first-tile address exception. The low and high tile
planes have separate reads, with their fetched background/window source held
through the row. See `window-counter-audit/counter-live-all/summary.json` and
`paru-irq-audit/window-fetch-netlist.md` for the independent phase investigation.

## First LCD clock edge

The pinned DMG netlist gives the causal path SACU-clocked horizontal counter,
XAJO, WUSA, TOBA/SEMU. The first CP edge must enable that path; subsequent edges
pass through already-enabled TOBA. LCD data itself remains combinational.
The implementation samples the first enable edge and running clock edges in
distinct discrete phases. It passes the phase into palette sampling without
mutating CPU-driven palette bus state. Mode 0 termination, LCD power transitions
and line initialization reset the enable state; window restarts do not.

The delay-0/delay-1 buckets are an inference from this ordering and the original
image collisions, not a measured analogue propagation delay. The preserved
`pixel-timing-audit/cp-edge-audit/control-only-source` is the source for the
21/24 control experiment. The prototype that modified palette bus state was
not merged. Real CPU-bus unit programs now distinguish the first clock edge
from an already-running stream for both LCDC and palette writes.

## WX=0 and seven real offscreen shifts

The final one-pixel mismatch came from WX=0 bypassing seven offscreen FIFO
clocks. In the original `m3_lcdc_win_en_change_multiple_wx` execution, row 0
WX=0 becomes visible at dot 84. The old match at dot 92 fetched window tile 0
at 93, then observed LCDC.5 clear at 94. The next tile at 99 was background,
yielding eight window pixels. The author's reference requires nine.

The same comparator now handles WX=0..6: `previsible_discard == 7 - WX`.
WX=0 matches at 85, latches window tiles at 86 and 92, before disable at 94.
Seven actual offscreen shifts leave `16 - 7 = 9` visible window pixels. The
first visible edge remains at 98 because earlier fetching and actual discards
offset each other. The retained trace is `window-pixel-audit/dots.txt`, with
the corresponding instruction trace beside it. Relevant original PCs are 0667
(WX write), 066A (LCDC off) and 066B (LCDC on).

Kevin Horton's *Nitty Gritty Gameboy Cycle Timing v0.01*, WX=00h section,
records B01B for seven clocks followed by W01 for six. Its window-address
section says the first window tile is retained. The
[attributed report mirror](https://gist.github.com/drhelius/3730564) is preserved
as a primary-author report mirror; the original blog URL was unavailable.
The report's half-clock convention and `%7` typography are not silently treated
as exact constants. This repair uses its access ordering and unchanged original
image evidence. The patterned unit fixture checks the last pixel of the first
window tile followed by the first pixel of the next, independently of uniform
colors or a specific ROM coordinate.

The first probe fixed the variable-WX image but caused 126 WX=0 timing errors:
one on every nonzero-fine-SCX row. Seven plus fine-SCX discards cross a FIFO
boundary. The discard branch fetched before popping, so a waiting load saw
depth one, refused to load, and required an extra refill-only dot. Popping first
permits the same-dot parallel load, matching the visible and initial dummy
paths. The independently observed nonzero-SCX activation wait remains.

`window-zero-probe/windows/summary.json` preserves the rejected intermediate
result. `window-zero-load-probe/all-images/summary.json` records the final
private 24/24 match, together with exact private source/binary hashes in
`window-zero-load-probe/build.json`.

An earlier CP-only extra-pixel hypothesis was rejected. In the netlist POVA
detects ROXY-qualified initial fine-SCX equality; window reset does not re-arm
ROXY. There is no justification for inventing an extra output pulse to repair
this fetch-provenance defect. No image, ROM instruction or expected timing
constant was altered.

All netlist references use revision
`dfbf8be3b11bbba71e4d54bfbd406150f572d9c0`, preserved with its file inventory in
`lcdc-pipeline-audit/dmg-schematics`. These are finite digital verification
results, not a proof of every half-cycle, oscillator or analogue behavior.
