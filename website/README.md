# Matchaboy product site

A static, cinematic product showcase with a native Three.js hardware scene,
scroll-driven exploded assembly, illustrative architecture HUD, actual desktop
captures, friend-play diagram, and links to the released Mac/Windows/Linux apps.

## Run

```sh
python3 -m http.server 4173 --bind 127.0.0.1 --directory dist
```

Open http://127.0.0.1:4173/. `dist` is authored source, not disposable build output.
There is no build step or runtime package installation. Three.js, GSAP, Phosphor
icons and fonts are pinned and self-hosted. License notices ship with assets.

## Verify

```sh
node --check dist/main.js
node --check dist/scene.js
python3 tools/verify_site.py
```

Run these commands from the `website/` directory. Visual direction and evidence
are in DESIGN.md and design-qa.md. Hosting account metadata is intentionally
excluded. This branch has no automatic website deployment.

The current scene uses Three.js. The requested Spline refactor is still pending;
this commit preserves the working cinematic prototype.

## Deployment approval

Local preview only until the user says they are satisfied and explicitly asks
for deployment. See the current deployment hold in DESIGN.md. Do not republish
after ordinary edits or enable automatic publishing on push.
