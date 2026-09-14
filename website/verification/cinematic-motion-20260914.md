# Exploded assembly motion — 14 September 2026

Local preview: http://127.0.0.1:4176/. Choose **Open the shell**, then **Replay opening** to watch the sequence. This pass was not published to the older temporary hosted preview.

## Change

- Replaced the orthographic, simultaneous sideways movement with a perspective camera following a continuous arc.
- Rear shell releases first. Front shell follows a lifted path with its own rotation. The PCB then turns into view; the final camera move eases around the assembly.
- Scroll ramps have zero velocity and acceleration at their endpoints. Drag rotation is damped, and reverse scrolling retraces the sequence.
- The key light crosses the shell during separation while a cool rim defines its edges. Shell and board materials have distinct reflections; PCB vias and capacitors add surface detail.
- Repeated chip pins, vias and cartridge contacts use instanced geometry to reduce draw calls. The existing off-screen rendering pause remains in place.
- Replay briefly reassembles the device, then opens it over a 4.7-second sequence. Changing scroll position cancels replay. It keeps the scene inside its sticky section when launched near the section's end.
- Reduced motion uses assembled/open stills and hides replay. No camera orbit or drag animation is applied in that mode.
- Short-phone layouts use compact component labels and reserve room between the model, replay button and chapter navigation.

## Verification

- Production TypeScript/Vite build passes.
- `node tools/verify-assembly-motion.mjs` passes on local Node 26.3.0. It independently projects the transformed part bounds through the perspective camera for **7,236 poses** across four aspect ratios and nine drag orientations. Maximum normalized viewport edge is **0.910**, below the 0.920 regression limit; all tested corners remain between the near and far planes.
- The same check covers staggered release, continuity at phase boundaries and reduced-motion stills. It checks framing of the conservative part bounds, not physical collision detection or a hardware-accurate teardown.
- Browser checks: 1440 × 960, 1920 × 980, 390 × 844 and 375 × 667. Closed, separating and settled replay frames inspected; keyboard replay, dragging and return-to-Play navigation exercised.
- Small-screen overlap found during review was corrected by compacting the component list and reserving vertical space for replay. No horizontal page overflow at the checked phone size.
- Browser phase markers (`data-assembly-phase` on the canvas) allow checks to wait for an actual animation stage rather than fixed screenshot delays.
- Device-level reduced-motion preference emulation was unavailable in the browser tool. The still-pose behavior is covered by the local motion check; no whole-site accessibility certification or universal frame-rate claim is made.

The model remains a procedural illustration of handheld hardware. The screen is the existing browser mini-game, not native ROM emulation. Native app files and dependencies were not changed. Visual acceptance remains the user's decision.

## Reference-led hardware rebuild, later 14 September

Reference: the user's `codex-clipboard-1e4cb4d8-4201-4504-a677-ef38cf70ec64.png`. This pass changes the actual interactive geometry and composition; it does not display the reference image as the scene.

- Replaced the simplified board with an extruded PCB with mounting holes, a four-sided 64-pin processor package, seven secondary chips, solder-ended passive components, capacitors, three metal ports and a raised speaker. Original canvas artwork supplies fine copper routing and silkscreen. Repeated metal details are instanced.
- Built a deeper rear housing with an inset floor, reinforcing ribs, bored screw posts, battery recess, divider, clips and metal battery contacts. Added a mating lip to the front shell.
- Added threaded 3D fasteners and dashed alignment guides that appear during separation, plus soft grounding shadows under the parts.
- Reoriented the final pose into aligned layers separated mainly in depth. The front shell moves toward the viewer; parts no longer fan into unrelated angles. The existing staged release, camera easing, reverse-scroll behavior and replay remain.
- Widened the desktop stage and corrected heading wrapping. At 1672 × 940 and 1440 × 800, the final heading stays on two lines. Added compact-phone spacing at 375 × 667; also inspected 390 × 844. Model detail necessarily becomes smaller in the narrow viewport.
- Selected Three.js's supported PCF shadow mode explicitly, replacing Fiber's deprecated soft-shadow default.

Validation: final TypeScript/Vite production build passes. The updated geometry bounds and deeper motion pass the same 7,236-pose projection regression, maximum edge 0.910. Browser inspection covered the closed assembly, opening, final exploded state, return to Play and keyboard replay. This validates sampled framing and visible behavior, not hardware accuracy, collision-free mechanics, universal 60 fps or visual acceptance. The lazy 3D bundle is approximately 265 kB gzip; the build still reports its large-chunk advisory.

Implementation: `src/HardwareDetails.tsx`, `src/hardware-primitives.tsx`, `src/HandheldScene.tsx`, `src/assembly-motion.ts`, and `src/styles.css`. This pass is a local website change and has not been deployed to a public URL.
