---
version: alpha
name: Matchaboy
description: A cartridge arcade and optional hardware workbench for Game Boy and Game Boy Advance.
colors:
  primary: "rgb(25%, 85%, 94%)"
  foreground: "rgb(85%, 90%, 94%)"
  muted: "rgb(48%, 60%, 67%)"
  positive: "rgb(45%, 90%, 60%)"
  warning: "rgb(100%, 65%, 32%)"
  background: "rgb(3.5%, 6%, 8%)"
  surface: "rgb(6.5%, 10.5%, 13%)"
typography:
  display:
    fontFamily: "Menlo, Consolas, monospace"
  utility:
    fontFamily: "Menlo, Consolas, monospace"
  native:
    fontFamily: "system-ui, sans-serif"
rounded:
  DEFAULT: "0px"
spacing:
  canvas-width: "1280px"
  canvas-height: "920px"
  header-height: "88px"
components:
  cartridge: {width: "370px", height: "110px"}
  play-action: {height: "46px"}
---

# Matchaboy design

## Overview

Matchaboy serves desktop players who want to play a cartridge and optionally
inspect its hardware. The approved reference is the existing Windows original
arcade (`games/verification/library.png` and `src/autopsy_windows.cpp`), carried
to macOS without a new visual identity. The language is English; no additional
locale or market-specific workflow is currently promised.

The signature is a two-column cartridge library beside a practical game guide.
Playing gives the LCD the largest area. Hardware instrumentation belongs behind
the Inspector action, never on the initial screen or the default player view.

`include/player_theme.hpp` owns the shared normalized RGB text/accent values and
GB palettes. Both native drawing adapters use it. This document mirrors that
source; change the shared header and this file together. Native menus, file
pickers, alerts and window chrome deliberately follow each operating system.

## Colors

Use dark blue-black surfaces, cyan headings and muted technical labels from the
existing Windows layout. Selection uses a visible cartridge edge and text, not
color alone. Grayscale is the default GB LCD palette; green is an explicit player
choice. GBA colors come from its actual framebuffer. Diagnostic waveforms and
palettes are observations, never invented decoration or substitute game images.

## Typography

Menlo on macOS and Consolas on Windows retain the established instrument-panel
character. Display titles use 25–27 canvas points, cartridge titles 19, body copy
15–16, and secondary hardware data 12–14. Native controls and menus use the system
font. Text must fit the same reserved areas without hiding Play or navigation.

## Layout

The 1280 by 920 canvas is proportionally scaled with a minimum usable window.
An 88-point header owns global navigation. Two columns contain the five GB and
five GBA cartridges; selection reveals description, controls and learning notes.
The player retains the original LCD aspect ratio and nearest-neighbor pixels.
Controls can be hidden. Inspector tabs share Video / CPU / Memory / Audio labels.

## Elevation & Depth

Static panels use tonal separation and thin cartridge edges. Window shadows and
modal presentation are owned by AppKit/Win32. Do not add decorative gradients,
floating overlays or shadows inside the game framebuffer.

## Shapes

Content panels and pixel-art cartridge marks stay rectangular. Native window
corners and platform button focus rings are intentional exceptions.

## Components

The catalog data and ordering come from `include/arcade_catalog.inc`; native
resource adapters expose the same `arcade_games()` / `arcade_rom_path()` API.
Mac actions use native accessible buttons and menus over the matching canvas.
Keyboard selection and pointer selection update the same library state. Play
loads the selected real ROM. Returning to the library pauses the current game;
returning to play restores its state. Selecting another game constructs it
before replacing the current one. Failed/cancelled file opens preserve progress.

Command-O/Command-L are Mac equivalents of Control-O/Control-L. Space pauses,
Tab switches Inspector, and the visible menus provide equivalents for shortcuts.
Losing window focus clears held inputs and playback. Mute and pause flush audio;
the application never changes system volume. Screenshots and file errors use
native dialogs with an explicit destination/recovery path.

There is no decorative animation. Game frames run on the hardware schedule;
the expensive diagnostic surface refreshes separately. Native semantics,
accessible labels, keyboard actions and visible selection are the baseline.
Verification is owned by `tools/test_player_macos.py`, native audio probes,
and interactive app checks; static design lint cannot prove those behaviors.

## Do's and Don'ts

- Keep the Windows and Mac library content, player hierarchy and Inspector names aligned.
- Show actual game output, memory, registers and audio observations.
- Keep screenshots and loading errors recoverable without losing the current game.
- Do not open the old all-panels inspector by default.
- Do not expose Windows GPU-registry preferences as nonfunctional Mac controls.
