# Readability and model correction — 14 September 2026

This pass updates the local site at http://127.0.0.1:4176/. The earlier temporary hosted preview was not republished in this pass.

The previous pass's claim of visual polish was premature. The user supplied desktop images showing undersized and low-opacity supporting text and an unsatisfactory model. This pass addresses those defects; user acceptance remains unconfirmed.

## Readability

Desktop browser measurements at 1920 x 980:

- Main story body: 21 px, opacity 1.
- Chapter navigation: 16 px, opacity 1, distinct active surface. Previously 10 px and 0.4 opacity.
- Platform note: 14 px.
- Netplay steps: 18 px; limitation note: 14 px.
- Making-of copy: 17 px.

Calculated representative text/background color pairs: body on paper 8.20:1; light navigation 9.43:1; active navigation 11.54:1; netplay details 6.80:1; dark body 10.19:1; dark navigation 12.97:1; download details 7.57:1. These checks cover the specified pairs, not a claim of whole-site accessibility certification. Text switches to stronger foreground colors during the background transition.

Browser checks covered 1920 x 980, 390 x 844, 375 x 667 and normal browser sizing. The mobile CTA wrap was corrected; at 375 x 667 the platform note ends at y=384.5, the scene starts at y=395, and chapter navigation ends at y=655.2, within the viewport. No text/model overlap remains in that checked hero layout.

## Model and layout

- Front shell now has an asymmetric rounded outline, extruded screen opening and speaker cutouts.
- Screen bezel surrounds an inset live canvas instead of sitting behind a flat screen plate.
- D-pad is a single bevelled shape. Labels use appropriately sized canvas textures; A/B and Start/Select remain readable.
- Rear shell is an open tray with ribs, screw posts and a battery compartment.
- Studio reflections and self-shadows replace excessive ambient fill. The hard backdrop shadow was removed after visual review.
- Live demo palette is muted and its shader now includes output color-space conversion. The browser mini-game remains distinct from the native emulator.
- Rendering pauses when the scene leaves the viewport. No universal frame-rate guarantee is made.
- Netplay instructions are grouped, enlarged and given a stable light surface. Excess vertical spacing was reduced.

Production TypeScript/Vite build passed. No browser errors were recorded during the model checks. Native app files were not changed in this correction pass.
