# Matchaboy product site

Audience: desktop players discovering Matchaboy. Primary outcome: download the
right released app. English marketing site, worldwide audience.

## Deployment hold — explicit user instruction

As of 2026-09-12, all further work must remain in local preview. The user must
first say they are satisfied and explicitly instruct deployment. Do not publish,
redeploy, or use automatic publication on source push before that approval.
The existing hosted version was published before this instruction arrived.
Use the local preview at http://127.0.0.1:4173/ for design review. Preserve this
hold across later tasks and iterations; general earlier auto-approval does not
override this newer instruction.

## Visual direction

The user chose the three Antigravity hardware references on 2026-09-12 and
explicitly prioritized their visual ambition. Preserve their obsidian green
palette, centered handheld silhouette, broad white headline, restrained glass
callouts, exposed circuit layers, and neon download deck. This supersedes the
first mint/coral direction. Do not revert to a generic light landing page.

The hero is a real Three.js scene, with shell, display, circuit board, rear case,
raised components, a dot-matrix screen shader, physical materials, an environment
map, rim lights and floating chips. Native scroll separates the model into four
layers beside the diagnostic panel. The illustrative PCB texture is generated;
the gameplay image and native app captures are actual credited product assets.
This is a software product illustration, not a physical product or schematic.

## Tokens and typography

Runtime authority: `dist/styles.css :root`. Background #060c09; surface #0d1913;
panel #16271f; accent #31ef98; soft green #b4f3cf; primary text #e2f0e7;
secondary text #93a99d; border #284337. Soft borders and restrained reflection
create glass depth. Reserve bright green for the display, interaction and CTA.
DM Sans is the body and hero face; Bricolage Grotesque supplies section headings.
Both fonts are self-hosted. Monospace labels belong to architecture metadata.

## Motion and interaction

GSAP ScrollTrigger fades and moves chapter text against native scrolling;
Three.js receives scroll progress for the exploded model. No scroll hijack,
loading gate, autoplay audio or invented benchmark counters. A real pause toggle
stops continuous canvas animation and removes GSAP motion. Reduced-motion starts
paused. Explicit rotate and assemble controls still work. The loop also sleeps
when the canvas is offscreen or the browser tab is hidden. DPR is capped at 1.6.
The mobile hero is recomposed, with the diagnostic panel stacked below hardware.
The app switcher shows real library/Inspector captures. Native disclosures reveal
Wi-Fi/internet connection details. Download links point directly to v0.1.0 ZIPs.

## Content boundaries

Local facts: sibling dmg README, DOWNLOADS, NETPLAY, HOMEBREW, and release
manifests. GB/GBA support, remappable controls, bundled homebrew and compatible
friend play are real. Do not claim online matchmaking, universal compatibility,
Apple notarization, awards, 50k FPS or desktop rollback. The included Tobu games
are single-player. Label the architecture HUD as a demo, not live emulator data.
Credits live at `dist/credits.html`. Preserve the native app's visual identity.

## Resilience and accessibility

Semantic links and buttons, visible keyboard focus, accessible icon-button names,
native disclosures, pause control, reduced-motion support, ordinary mobile
scrolling and no hover-only content. Static download content works without JS.
WebGL failure shows the PCB illustration and removes unavailable model controls.
Image dimensions are reserved; all assets are local. No analytics, forms,
authentication or tracking code is part of the site. Keep the hosted audience
owner-private unless the user requests wider sharing.

## Verification

See `design-qa.md` for reference comparisons and `verification.json` for checks.
Browser validation covers 1376x768 desktop and 390x844 mobile, plus intermediate
widths for layout overflow. Source audits are supplemental, not visual proof.
