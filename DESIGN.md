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
to macOS and Linux without a new visual identity. The language is English; no additional
locale or market-specific workflow is currently promised.

The signature is a two-column cartridge library beside a practical game guide.
Playing gives the LCD the largest area. Hardware instrumentation belongs behind
the Inspector action, never on the initial screen or the default player view.

`include/player_theme.hpp` owns the shared normalized RGB text/accent values and
GB palettes. The platform drawing adapters use it. This document mirrors that
source; change the shared header and this file together. Native menus, file
pickers, alerts and window chrome deliberately follow each operating system.

## Colors

Use dark blue-black surfaces, cyan headings and muted technical labels from the
existing Windows layout. Selection uses a visible cartridge edge and text, not
color alone. Grayscale is the default GB LCD palette; green is an explicit player
choice. GBA colors come from its actual framebuffer. Diagnostic waveforms and
palettes are observations, never invented decoration or substitute game images.

## Typography

Menlo on macOS, Consolas on Windows and monospace on Linux retain the
established instrument-panel character. Display titles use 25–27 canvas points, cartridge titles 19, body copy
15–16, and secondary hardware data 12–14. Native controls and menus use the system
font. Text must fit the same reserved areas without hiding Play or navigation.

## Layout

The 1280 by 920 canvas is proportionally scaled with a minimum usable window.
An 88-point header owns global navigation. Two columns contain the five GB and
five GBA cartridges; selection reveals description, controls and learning notes.
The player retains the original LCD aspect ratio and nearest-neighbor pixels.
Controls can be hidden. Windows/Mac Inspector tabs share Video / CPU / Memory /
Audio labels. Linux currently exposes a combined CPU/FIFO/memory inspector; do
not label missing tabs or dot stepping as implemented.

## Elevation & Depth

Static panels use tonal separation and thin cartridge edges. Window shadows and
modal presentation are owned by AppKit/Win32 or the Linux desktop window manager. Do not add decorative gradients,
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

Command-O/Command-L are Mac equivalents of Control-O/Control-L on Windows.
Linux uses Ctrl+O for Open and exposes Library in its File menu.
Escape pauses local play on every platform. Tab switches Inspector and visible
menus provide alternatives. With the platform modifier (Command on Mac, Ctrl
on Windows/Linux), Shift-C toggles the guide, Shift-M toggles sound, P switches
the GB palette and S/F step instruction/frame. Windows/Mac also provide
modifier-D for GB dots and modifier-1–4 for Inspector tabs; Linux currently does not.
Losing window focus clears held inputs. Local play pauses; an active friend session keeps its network schedule running. Mute and pause flush audio;
the application never changes system volume. Screenshots and file errors use
native dialogs with an explicit destination/recovery path.

Each player guide presents **Game button → Press key** using the active
mapping. The default Balanced layout is WASD for directions, L for A, K for B,
Q/I for shoulders, Enter for Start and Space for Select. Classic preserves the
earlier arrows/Z/X/Shift/Q/W game layout; both use the same emulator shortcuts.
Space does not fast-forward. `include/keyboard_mapping.hpp` owns the shared
presets, physical ANSI names, aliases, lookup, validation and platform key
translation. The Mac compatibility include preserves the existing preference
version. Each frontend supplies events, persistence and guide labels.

**Matchaboy > Keyboard Settings…** (Command-Comma) on Mac and
**Tools > Keyboard Settings…** (Ctrl-Comma) on Windows/Linux edit one draft. A binding captures one key; **Set all keys…** walks through
the game buttons. Balanced/Classic selection updates the draft. Duplicate or
reserved keys produce inline correction text. **Apply** validates and persists
the mapping in app-specific preferences (NSUserDefaults on Mac, a single
HKCU registry record on Windows, an atomically replaced XDG configuration file
on Linux); **Cancel** leaves the previous
mapping intact. Applying updates the guide without resetting a game. Capture
events belong only to settings, and held game keys are cleared around the flow.
Mappings remain outside emulator snapshots and network state; peers exchange
logical console buttons and can choose different keyboard layouts.

A visible `*` and positive color mark locally held keys, with an explanation
that does not rely on color alone. The guide does not guess game-specific actions
or claim that a local key has already reached the peer. Settings show physical
ANSI positions rather than characters produced by the selected input method.

There is no decorative animation. Game frames run on the hardware schedule;
the expensive diagnostic surface refreshes separately. Native semantics,
accessible labels, keyboard actions and visible selection are the baseline.
Verification is owned by `tools/test_player_macos.py`, native audio probes,
and interactive app checks; static design lint cannot prove those behaviors.

## Do's and Don'ts

- Keep Windows, Mac and Linux library content, player hierarchy and Inspector names aligned.
- Show actual game output, memory, registers and audio observations.
- Keep screenshots and loading errors recoverable without losing the current game.
- Do not open the old all-panels inspector by default.
- Do not expose Windows GPU-registry preferences as nonfunctional Mac controls.

## Friend play

Netplay extends each player's menu and follows its Open Game dialog ownership.
Mac uses AppKit, Windows uses native Win32 controls, and Linux keeps settings
and connection forms in its X11 event loop. Host Game and Join Game use labeled fields, masked room codes, inline
validation, and an explicit Copy room code action. Codes stay in memory and are
never written into captures or logs. Starting a session saves local progress and
prepares a fresh console before replacing the current game. Address resolution
runs off the UI thread; connection preparation preserves the old game on error.
Cancel resumes the old game immediately and discards the eventual result without
waiting on the UI thread. A second attempt waits until the first lookup resolves.

The existing player footer owns connection status, player number, linked frame,
and persistent failure text. Its Pause action becomes Disconnect. The Inspector
uses the same status footer, while Connection Details provides the full status
and network instructions. Status also appears in the view's accessibility help.
No new colors, fonts, decorative panels, or framebuffer overlays are introduced.

Connection Details has separate **Copy room code** and **Copy diagnostics**
actions. Diagnostics contain operational status, current/last verified frames,
queued inputs, sent and accepted received packets, retries, peer silence, and
App Nap prevention state on Mac. They exclude code, address, ROM path, and save data.
Persistent failure copy tells the player to copy diagnostics before Disconnect,
which discards the session. No automatic reconnection or inferred failure cause
is presented.

An unfinished friend session owns one
`NSActivityUserInitiatedAllowingIdleSystemSleep` activity; finish, disconnect, or
presentation destruction releases it. This suppresses App Nap while allowing
normal system sleep. Network service runs on the nominal 240 Hz UI timer even
when no video frame is due, including `NSModalPanelRunLoopMode`. The ten-second
peer-silence threshold and 100 ms retry timer are unchanged. Three-second real
packet gaps and polling pauses recover in transport tests; the user's reported
intermittent disconnect has no confirmed diagnosis yet. Windows modal settings
and connection loops dispatch the owner timer; Linux continues servicing linked
frames in its event loop during dialogs. These mechanisms must be verified on
their own platform; a successful Mac run does not verify either implementation.

A linked game cannot pause, step hardware, open another ROM, or enter the library.
Disabled controls have native disabled semantics and the canvas names the ROM
lock. Palette, Inspector panels, controls visibility, mute, and screenshots remain
available. Losing focus clears held input while the link keeps running. Disconnect
detaches the cable before saving the local console and resuming ordinary play.
The replicated friend's console never writes the local save file.

Two-player connection is explicit: the host shares its LAN/VPN address, UDP port,
and room code. Internet use requires a reachable VPN address or host port
forwarding. The interface does not promise automatic router setup, public relays,
or encrypted transport. A room token selects a session; it does not verify a
person's identity. Single-player cartridges do not become multiplayer games.

## Platform verification boundary

Windows and Linux carry the player workflow and shared keyboard/session model,
not every other platform's implementation detail. Windows GPU selection remains
Windows-only; Linux uses X11/Xft and optional ALSA. The Linux headless capture exports
the real LCD, while a desktop capture observes the X11 window. No unrendered view
is represented as a captured GUI. Native Windows dialog tests and Linux X11
input tests gate downloads, including keyboard navigation, saved mappings and
live links during settings. Physical audio and compositor-specific behavior
remain separate device checks.
