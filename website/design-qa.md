# Design QA — Matchaboy cinematic redesign

Date: 2026-09-12. Browser: Codex in-app browser on the host Mac.
Primary comparison viewport: 1376 × 768, matching the three supplied concepts.
Mobile: 390 × 844. Intermediate overflow checks: 768 × 1024 and 1024 × 768.
The screenshot set is in `qa/`. Supplied references remain outside the repository.

## Reference comparisons and iterations

The selected Antigravity hero, autopsy and netplay/download images were opened
and compared with rendered browser captures together in the same visual inputs.
A second agent independently reviewed the hero and hardware comparisons.

1. Hero: replaced the mint layout with obsidian, large white centered type, a
   dark handheld, four glass callouts and green CTA lighting. The first render
   put the device under the navbar; reduced its scale and lowered it. Expanded
   the title across the screen. Darkened buttons, increased yaw and rim light,
   and added actual shadowed mesh geometry. The source's composition is retained
   with the real product logo and gameplay capture. P1 proportion issues fixed.
2. Autopsy: implemented separate chassis/LCD/PCB/rear groups, a real screen hole,
   independent tilted LCD, textured board with raised components, and shadows.
   Fixed heading/model overlap, moved and enlarged the assembly, and made the
   HUD fit a desktop viewport. Strengthened glass edges and used a sparse grid.
   The result interprets the reference as an interactive model; geometry and
   layout intentionally accommodate responsive web rendering. P1 overlap fixed.
3. Downloads: used the reference's three-platform glass/neon deck, while retaining
   real release metadata. Added inset green light and rim highlights to cards
   and buttons. The netplay diagram and download deck have their own sections
   instead of the reference's combined benchmark/comparison poster.
4. Mobile: reflowed the headline, shortened the nav action, removed the peripheral
   hero callouts, moved the HUD below the object and hid decorative layer labels
   that collided with the enlarged chassis. No horizontal overflow was observed.
5. Runtime QA found an intro/scroll tween conflict that hid the hero headline
   after a resize. Moved intro animation to child elements and made the scroll
   tween's starting opacity explicit. Rechecked at 768 × 1024: opacity 1, visible
   title and CTA. Fixed and recaptured.

## Interaction and accessibility evidence

Tested native anchor navigation, model rotation, assemble/explode states,
animation pause, app screenshot switching, the internet disclosure, and the
three actual download destinations. Local images all loaded successfully.
New runtime console warnings/errors were absent in the final pass. Historical
warnings from the initial implementation were corrected and not counted as
final results. Source audits passed without findings. The pause mode uses stable
chapter poses, stops ambient loops, and retains deliberate rotate/assemble actions.
Offscreen scheduling and shadow cleanup were inspected and corrected.

## Known scope

No full browser matrix, automated WCAG certification or performance benchmark
was performed. The system-level reduced-motion setting was not changed; its
code path was reviewed and the equivalent explicit pause state was exercised.
The 3D model, PCB and HUD are clearly illustrative. This is an implemented visual
interpretation, not a promise of exact reproduction of the generated references.
