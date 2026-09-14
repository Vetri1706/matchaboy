# Matchaboy console sequence — 14 September 2026

Open the local site at http://127.0.0.1:4176/ and choose **Follow the frame**.
The interactive evidence gallery is http://127.0.0.1:4176/__review/report.html.
This pass is local; it was not published or pushed.

## Scope

The console sequence keeps the existing Three.js / React Three Fiber, GSAP and Lenis stack. The rest of the page and native app were not redesigned.

- `src/assembly-motion.ts`: explicit assembled/opened positions, a shared separation axis, independent nested drag rotation, perspective bounds fitting.
- `src/presentation.ts`: one progress owner for scroll and replay, read by both scene and chapter content.
- `src/HandheldScene.tsx`, `src/model-geometry.ts`: named shell/screen/controls/board groups, A/B recesses, glass, distinct materials and fixed studio lighting.
- `src/App.tsx`, `src/styles.css`: reserved text region, compact phone composition, keyboard play/pause path and recorded Inspector handoff.
- `src/ScenePoster.tsx`, `public/scene`: actual canvas stills for loading, reduced motion, unavailable 3D and an optional still view. They do not replace the interactive scene by default.
- `src/Inspector.tsx`: recorded frame 154 → 155, with the real difference of 70,220 clock cycles and 5,853 instructions. This is explicitly separate from the browser mini-game.

## Evidence

`desktop-*` and `mobile-*` contain 36 unmodified canvas captures and their DOM layout/pose metadata: 0%, 12.5%, 25%, 37.5%, 50%, 62.5%, 75%, 87.5%, 100%, in both directions, at 1672 × 940 and 375 × 667. These are canvas captures, not reconstructed whole-page screenshots. Full page views were additionally inspected in the browser.

`checks.json` records the audit. Text/scene clearance is at least 28 px on desktop and 6.4 px on the narrow phone. No sampled horizontal overflow or navigation intersection. Small forward/reverse differences arise from scroll-pixel quantization and settling tolerance; maximum numerical pose difference is 0.00434.

The independent projection check passes 7,236 poses, including nine drag orientations across four aspect ratios. Bounds remain inside 91% of the viewport. Axis consistency, separated transform ownership, replay interruption and reverse convergence pass.

Browser actions checked: drag, replay through the actual release phase, keyboard play, Escape returning focus, Tab/Enter on Pause & open, recorded Inspector focus and frame change, keyboard Download to visible release choices, reduced-motion branch, simulated failure and Enable 3D recovery. The GitHub release API confirmed the three linked ZIP files. Downloads/installations were not repeated.

The normal-site browser run logged no errors. An upstream `THREE.Clock` deprecation warning remains. Production build passes with Vite's existing large-scene-chunk advisory.

## Measurements and limits

At the opened pose, 119 intervals from 120 frames in this machine's in-app browser:

| Viewport | Frame interval median / p95 | CPU render submission median / p95 |
|---|---|---|
| Desktop | 11.0 / 16.4 ms | 3.7 / 7.6 ms |
| Narrow viewport | 7.2 / 10.2 ms | 4.2 / 7.1 ms |

Reported main render: 337 calls, 163,784 triangles, 193 geometries, 25 textures. DPR 1. Development build with canvas preservation for capture; CPU submission is not GPU execution time. GPU identity was hidden as `WebKit WebGL`. The narrow viewport ran on the same computer, not a phone.

Procedural geometry/texture generation means there is no external GLB or HDRI download. Closed/open posters are 31,506 / 67,222 bytes. The lazy scene bundle is approximately 981 kB raw / 263 kB gzip, including renderer dependencies. Full bundle measurements are in `checks.json`.

Not completed: physical-phone/touch testing, OS-level reduced-motion emulation, GPU timing, field Core Web Vitals or cross-browser certification. The reduced branch was exercised using a development control. The model is an illustrated handheld, not a mechanically verified circuit layout. Browser mini-game output is not native ROM emulation. Visual acceptance remains the user's decision.

## Reproduce

With the site development server running, add `?review` to use the local-only capture controls. They seek the real scroll input, wait for presentation/canvas settling and save through a fixed-path Vite development endpoint. The controls and endpoint are absent from the production experience.

Run `node tools/verify-assembly-motion.mjs`, `npm run build`, then `node tools/report-sequence.mjs`. The final command audits the saved evidence and regenerates the gallery; it does not drive the browser or manufacture screenshots.
