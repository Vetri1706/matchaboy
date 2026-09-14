# Checkpoint validation — 14 September 2026

This checkpoint preserves the current WebGL website and existing native sprout branding before a separate video-reveal experiment.

- Website dependency install: 90 packages audited, zero reported vulnerabilities.
- TypeScript/Vite production build passed; existing large-scene-chunk advisory remains.
- Camera/ownership verification: 7,236 poses passed.
- Native Release build with MATCHA_BUILD_AUTOPSY=ON: all 15 CTest suites passed (40.84 seconds).
- verify_autopsy.py: Moon Courier used explicitly because the default external Acid2 fixture is absent. Headless and GPU captures matched exactly: zero channel error over 220,800 samples, including 69,120 LCD samples.
- test_autopsy_windows.py: all 15 listed dashboard interaction checks passed.
- Windows executable SHA256: 5bbde83ffd40a6bb98dca5d1832b00e3889df61630dcf919cf555914484d0273.
- Fresh native evidence: workspace outputs/checkpoint-20260914-223110/{render,interaction}.
- macOS/Linux branding source assets are included; those binaries were not rebuilt here.

Earlier browser checks and their scope/limits are recorded in sequence-20260914/README.md. No deployment or push is implied by the local commit.
