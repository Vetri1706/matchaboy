# Matchaboy website

The preserved interactive console website lives here. It uses the existing React, Three.js / React Three Fiber, GSAP and Lenis stack. The console contains a browser mini-game; the Inspector demonstration uses labelled native captures, not live browser ROM emulation.

From this folder, run `npm ci`, then `npm run dev -- --host 127.0.0.1 --port 4176`. Build with `npm run build`.

The original working preview remains in the sibling `work/matchaboy-webgl-site` folder. This repository copy checkpoints that version before the separate video experiment. Dependency folders, generated builds and local deployment credentials are excluded from Git.

## Verification

- [Console sequence and 36 captured positions](verification/sequence-20260914/README.md)
- [Checkpoint validation](verification/checkpoint-20260914.md)
- `node tools/verify-assembly-motion.mjs` checks camera framing, transform ownership and reverse/replay behavior.
- The development-only `?review` controls can regenerate captures. `node tools/report-sequence.mjs` builds the local evidence gallery after capture and production build.

## Content and licensing

The handheld board is an illustration, not a verified hardware schematic. The included Tobu screenshots are credited in [public/captures/CREDITS.txt](public/captures/CREDITS.txt). Workshop games are separate source experiments. Downloads point to the existing v0.1.0 releases; this website checkpoint does not publish a new app release.

The sprout identity comes from the maintainer-supplied reference. `node tools/prepare-brand.mjs` updates this site's icons and the parent native repository's branding assets. Run that explicitly only when updating the identity.

See the parent repository LICENSE and THIRD_PARTY.md. No new deployment was performed for this checkpoint.
