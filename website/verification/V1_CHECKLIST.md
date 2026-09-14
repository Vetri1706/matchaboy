# Matchaboy v1 final checklist

Verified 14 September 2026. The original interactive website is published and public. The downloads now serve **v0.1.1**, the sprout branding update for all three desktop platforms. This is not a desktop v1.0 release.

## Published version

- [x] [Public website](https://matchaboy-play.vetrikalanjiyam.chatgpt.site/) opens without a sign-in gate. Anonymous HTTP requests also succeeded.
- [x] Original port-4176 experience deployed. The separate port-4177 video experiment was preserved and excluded.
- [x] Website presentation and assets retain checkpoint `058a8599e34f7a0d9e2b030ebc2daf7b87ed2ebf`; only current desktop release references changed to v0.1.1.
- [x] Production source saved in the separate `work/matchaboy-v1-site` folder; Sites source commit `d9ea343622512fec7615eb4413298759fd0b678b`.
- [x] Sites version 4 deployed successfully: `appgdep_6aa83910cec08191afffb5d3e63c0e58`.

Native release source is published at tag `v0.1.1`, commit `73b49c9ead93111fd85abf2ae6f284d0bda522b7`, on `release/sprout-branding`. The original presentation was preserved.

## Website verification

- [x] Clean dependency installation and production TypeScript/Vite build passed. Installation audit reported zero known vulnerabilities.
- [x] Motion verifier passed 7,236 framing/rotation poses, assembly-axis checks, transform ownership, reverse convergence and replay interruption checks.
- [x] Live desktop/tablet-width and 375 × 667 browser layouts checked. Opening and exploded composition render; text is separate from the object; narrow layout has no horizontal document overflow.
- [x] Live chapter navigation, optional still view, 3D restoration, Inspector handoff, keyboard recorded-frame stepping and Memory tab checked.
- [x] Recorded Inspector advances from frame 154 to 155: 70,220 clock cycles and 5,853 instructions. Captures are explicitly described as recorded output.
- [x] Workshop carousel navigation works; visible GB previews load. Offscreen images load lazily. All ten preview assets were independently downloaded and matched the production build.
- [x] Download anchor and three platform destinations present. Production page contains no video element; the experiment was not accidentally included.
- [x] 52 asset/documentation URLs returned HTTP 200. All 42 non-HTML site files matched build hashes. HTML matched after accounting for the hosting edge's injected Cloudflare browser-check script. Nine GitHub destinations also responded successfully.
- [x] No JavaScript errors observed in the live smoke test. Existing upstream `THREE.Clock` deprecation warning remains.

Earlier checks for the same source, including nine evenly spaced positions in both directions, keyboard play/Escape, reduced-motion branch, simulated 3D failure and recovery, are recorded in [sequence-20260914/README.md](sequence-20260914/README.md). These are prior local checks, not all repeated after deployment.

### Performance evidence and limits

Production bundles: main JS 356.68 kB / 120.26 kB gzip, lazy scene 980.67 kB / 265.38 kB gzip, CSS 33.75 kB / 8.63 kB gzip. Vite's large-scene-chunk advisory remains. Geometry is generated in code, so there is no separate GLB download.

Earlier local measurements on this Windows computer: frame interval median/p95 11.0/16.4 ms at desktop size and 7.2/10.2 ms at narrow size; 337 render calls and 163,784 triangles. These are not physical-phone measurements, GPU timings or field Core Web Vitals. No universal 60 fps claim is verified.

## Current desktop release: sprout branding

- [x] [v0.1.1](https://github.com/Vetri1706/matchaboy/releases/tag/v0.1.1) published 14 September 2026. [Release CI](https://github.com/Vetri1706/matchaboy/actions/runs/34878471317) passed Windows, macOS and Linux builds, runtime tests and extracted-package checks.
- [x] Sprout header in all native apps; Windows EXE icon, Mac ICNS and Linux SVG/EWMH task-switcher icon updated. Linux's native screenshot and window property check passed on X11.
- [x] All three ZIPs downloaded and checked: published SHA256, ZIP integrity, every BUILD_INFO file hash, clean source revision and packaged brand assets match. The anonymous public download URLs and checksums returned HTTP 200.
- [x] The downloaded Windows EXE was executed locally: 220,800 render comparisons and 69,120 LCD samples matched exactly. Its Inspector screenshot visibly contains the sprout mark. All six embedded Windows icon resolutions match the source ICO.
- [x] Website download links now target v0.1.1. Installation and platform limitations listed below still apply. Existing local copies do not update automatically.

Evidence: workspace `outputs/sprout-release-20260914/packages.json`, `public-release.json`, `public-downloads.json`, `downloaded-windows-render/summary.json`, and `linux-verified/artifacts/ci/linux-player/summary.json`. Fresh macOS runtime checks ran in CI, not on this Windows host.

## Earlier v0.1.0 verification

[Previous release: v0.1.0](https://github.com/Vetri1706/matchaboy/releases/tag/v0.1.0), published 12 September 2026, built from `c2da3a627ce9270d6c964a764f4000b15a9d9517`. The records below refer specifically to that earlier release; its historical assets were retained.

| Package | Fresh verification | Installation / limits |
| --- | --- | --- |
| [Windows x64](https://github.com/Vetri1706/matchaboy/releases/download/v0.1.0/matchaboy-windows-x64.zip) | Download, published SHA256 and ZIP integrity passed; launched and tested locally | Windows 10/11, unzip and run Matchaboy.exe. No installer or separate mGBA/BIOS package. Unsigned publisher. |
| [macOS ARM64](https://github.com/Vetri1706/matchaboy/releases/download/v0.1.0/matchaboy-macos-arm64.zip) | Download, published SHA256 and ZIP integrity passed; release CI evidence reviewed | Apple Silicon, deployment target macOS 13+. Ad-hoc signed, not notarized; follow Start Here. Actual macOS 13 minimum was not tested here. |
| [Linux x64](https://github.com/Vetri1706/matchaboy/releases/download/v0.1.0/matchaboy-linux-x64.zip) | Download, published SHA256 and ZIP integrity passed; release CI evidence reviewed | X11 desktop with X11/Xft system libraries; retain adjacent assets. Optional ALSA. Not a zero-system-dependency Linux package. |

All packages include BUILD_INFO, Start Here, LICENSE, THIRD_PARTY.md, corresponding source and third-party notices. Minimum CPU/RAM and a broad GPU compatibility matrix have not been established.

### Fresh Windows smoke tests against the downloaded release

- [x] GB and GBA audio with Inspector open: device opened, nonzero PCM, completed speaker buffers, **zero underruns and zero dropped frames** in each eight-second fixture run. Mute cleared queued audio; pause stopped it.
- [x] GB completed 373,440 frames of audio; GBA completed 380,160, at 48 kHz. This establishes device playback, not a human listening verdict for every commercial game.
- [x] GBA booted an authored ROM in a Unicode path without external BIOS; A/L/R changed rendered pixels.
- [x] GBA Inspector's four panels, memory paging, CPU instruction/frame stepping and paused state passed. Unsupported dot stepping was ignored correctly.
- [x] GBA cartridge SRAM survived clean close/reload and was read and incremented by the second boot.

The newer local native checkpoint separately passed 15 native suites, exact headless/GPU comparison and 15 Windows dashboard checks; see [checkpoint-20260914.md](checkpoint-20260914.md). Those results do not mean its newer binary has been released.

## Claims that are safe to make

| Feature | Verified boundary |
| --- | --- |
| GB | Original Matchaboy engine; native fixtures and bundled-game evidence available. Broad commercial compatibility is not guaranteed. |
| GBC | Game Boy compatibility mode only. Full Game Boy Color / GBC-exclusive support is not complete. |
| GBA | Statically integrated mGBA 0.10.5; native playback, input, audio, save and Inspector fixtures pass. |
| Saves | Cartridge save persistence; use a writable location. Fresh test here covers GBA SRAM, not every cartridge/save technology. |
| Controls | Keyboard controls/remapping. Physical gamepad/controller support is not implemented or verified. |
| Inspector | GB CPU/memory/PPU/audio and GBA inspection work within platform-specific capabilities. Linux's combined layout differs from Windows/macOS. |
| Netplay | GB/GBA host/join and native UDP/friend-session tests exist and passed in release evidence. Requires a reachable IPv4 address, VPN or forwarding. No hosted matchmaking, relay or automatic NAT traversal. Broad two-machine internet play/long-session testing remains open. |
| AI gameplay | Development direction; no production AI gameplay demonstration is verified. Astra is credited as a development collaborator. |
| Website screen | Original browser mini-game mapped to the console, not native ROM emulation. Inspector evidence is separate, authentic recorded app output. |

## Credits, rights and source

- [x] [Committed GNU license](https://github.com/Vetri1706/matchaboy/blob/feature/original-arcade/LICENSE) and [native third-party credits](https://github.com/Vetri1706/matchaboy/blob/feature/original-arcade/THIRD_PARTY.md) work. Native credits cover mGBA (MPL-2.0), blip_buf (LGPL-2.1-or-later) and inih (BSD).
- [x] The two included games are **Tobu Tobu Girl** and **Tobu Tobu Girl Deluxe**, by Tangram Games, music by potato-tan. Official ROMs, source/credits and licenses are included: game code MIT, assets CC BY 4.0. Deluxe runs in GB compatibility mode.
- [x] The ten original GB/GBA games are clearly identified as source test cartridges, separate from the two games shipped in v0.1.0. [Provenance](https://github.com/Vetri1706/matchaboy/blob/feature/original-arcade/games/PROVENANCE.md) distinguishes original contributions and external tools.
- [x] Commercial games are not bundled. Users supply their own legally obtained games. No Nintendo affiliation is claimed.
- [x] [Bug reports](https://github.com/Vetri1706/matchaboy/issues), [source](https://github.com/Vetri1706/matchaboy) and [netplay guide](https://github.com/Vetri1706/matchaboy/blob/feature/original-arcade/NETPLAY.md) are reachable.
- [ ] Before closing v1, add a dedicated website dependency/notices inventory. The linked THIRD_PARTY.md covers the native app; the site additionally uses React, Three.js, R3F/Drei, Lenis and GSAP. GSAP declares its own standard no-charge license, not MIT. A complete transitive website notice audit was not completed.
- [ ] Make the project-wide license grant/copyright attribution explicit for release metadata. The website package and original-game provenance identify GPL-3.0-only, but the root LICENSE by itself is the standard GPL v3 text. Do not mistake the FSF's license-document copyright for ownership of Matchaboy's source.

## Remaining v1 release work

1. **Completed:** publish the sprout desktop branding as v0.1.1 for Windows/macOS/Linux and update the public website download links. No v1.0 tag was created.
2. Close the website dependency-notice and explicit licensing/attribution items above.
3. Run physical-phone/touch and Safari checks, ordinary long gameplay/listening sessions, and a two-computer netplay soak. These are unverified, not passed by resizing this browser or running short fixtures.
4. Keep full GBC, physical gamepads and AI gameplay out of v1 claims until separately implemented and tested.

## Evidence retained locally

Workspace `outputs/v1-final-check-20260914/` contains `release.json`, `packages.json`, `public-links-and-assets.json`, all downloaded ZIPs, their build records, and fresh `release-audio-inspector/summary.json` and `release-gba-save/summary.json`. The deployment archive is `outputs/matchaboy-v1-original.tar.gz` (SHA256 `9fb374698fe6957735c1f314a202eb6720c4a8be2708e7dfd5132bf08115d30d`).

This checklist records tested behavior and open work. It is not a claim of complete compatibility, physical-device certification or an award rating.
