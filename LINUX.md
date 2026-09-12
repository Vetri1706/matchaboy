# Matchaboy on Linux

The Linux desktop player opens the same ten original GB/GBA games, a cartridge
player, Inspector, keyboard settings and Host/Join friend play. It uses native
X11 drawing, Xft/Fontconfig text and the existing Matchaboy DMG and bundled mGBA cores; it does not
introduce Qt, SDL or another emulator framework. A normal X11 desktop or an
XWayland session with evdev-style keycodes is required for windowed play.

The Ubuntu 24.04 native workflow verifies actual X11 input and rendering,
mapping persistence, save-failure recovery and 1,200 linked frames, including
12 seconds with Settings open. See the [native run](https://github.com/Vetri1706/matchaboy/actions/runs/34681561459)
and the exact verification manifest in each download's `Extras/BUILD_INFO.json`.
Xvfb checks do not prove physical audio playback or a particular Wayland compositor.

## Build and open

On Debian/Ubuntu, install a compiler and X11/Xft development files; ALSA development
files enable speaker output:

```sh
sudo apt-get install clang cmake ninja-build pkg-config libx11-dev libxft-dev libasound2-dev
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release -DMATCHA_BUILD_AUTOPSY=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
./build/Matchaboy
```

From an extracted Linux player package, run `./Matchaboy`. If the archive tool
did not preserve executable permission, use `chmod +x Matchaboy`. Keep the
packaged resources and license/source files together. The runtime needs the
X11 and Xft libraries; ALSA is optional at build time. Without an available audio device,
the game continues silently. No system volume setting is changed.

Select a library game and choose **Play**, or use **File > Open game…** / Ctrl+O.
The file browser navigates folders and loads `.gb` and `.gba` cartridges. You can
also pass a path directly:

```sh
./build/Matchaboy /path/to/game.gba
./build/Matchaboy --game matcha-garden
```

Bundled games are prepared in `$XDG_DATA_HOME/Matchaboy/Library`, falling back
to `~/.local/share/Matchaboy/Library`, so their saves do not modify installed
resources. Cartridge progress uses `<game>.matchaboy.sav` beside the writable ROM.
If a save fails on quit, the game remains in memory. Fix the folder and retry,
keep playing, or explicitly choose Quit without saving.

## Keyboard settings

Choose **Tools > Keyboard settings** or press **Ctrl+,**. The player also has a
visible settings button. Click a game-button binding, then press the replacement
key. **Set all keys** captures the buttons in sequence. Apply persists a valid
mapping; Cancel keeps the previous one. Duplicate/reserved keys need correction
before Apply. Escape cancels key capture first.

| Console control | Balanced default |
| --- | --- |
| D-pad up / left / down / right | W / A / S / D |
| A / B | L / K |
| L / R shoulders (GBA) | Q / I |
| Start / Select | Enter / Space |

Balanced restores these defaults. Classic selects arrows, Z/X, Enter/Shift and
Q/W shoulders. Bindings use physical key positions; the guide updates to the
saved layout. Space is Select, not fast-forward. Escape pauses local play,
Tab opens Inspector, F12 captures, and Ctrl+Shift+C toggles the controls guide.
Ctrl+Shift+M toggles sound, Ctrl+P switches the GB palette, and Ctrl+S/F steps
instructions/frames while offline. The current Linux inspector combines real
CPU, FIFO and memory observations; it does not yet provide the separate four
Windows/Mac tabs or GB peripheral-dot stepping. Application shortcuts use Ctrl
where the Mac player uses Command.

Settings are stored as a validated, atomically replaced file at
`$XDG_CONFIG_HOME/matchaboy/keyboard-v1.conf`, falling back to
`~/.config/matchaboy/keyboard-v1.conf`. They belong to this application and user,
not the system keyboard. Held gameplay inputs clear when opening/closing
settings or losing focus. A linked session continues to service the peer while
settings or another app has focus.

## Friend play

Use the **Netplay** menu to Host, Join, inspect the connection, or Disconnect.
Both computers need matching builds and the exact same ROM revision. Share the
host's reachable IPv4 address, UDP port (normally 27888) and room code. Open the
game's own multiplayer/link menu after the app connection is established.

The implementation uses the same real GB/GBA FriendSession as Mac and Windows.
Different keyboard layouts are fine: only logical console buttons cross the
network. This is not a claim that every mixed-platform game combination has
been tested. The retained FIFA evidence is specifically from Mac processes and
the user's two-Mac report. See [NETPLAY.md](NETPLAY.md).

Settings and connection dialogs keep the event loop running. Connection Details
can copy diagnostics without the room code, address, ROM path or save contents.
Ten seconds without valid peer traffic still terminates a session. Internet
play requires a reachable VPN or explicit host UDP forwarding; there is no
relay, automatic NAT setup or transport encryption.

## Capture and verification

The player accepts `--paused`, `--inspector`, `--green`, `--hide-controls`,
`--frames N`, `--buttons MASK`, `--input-frames N` and `--capture FILE.png`.
Its `--headless` mode records actual LCD pixels and JSON metadata without an
X server. It does not pretend that LCD-only output is a full desktop screenshot.
`--window-test` captures the actual X11 window and is suitable for Xvfb in CI.

```sh
./build/Matchaboy --game matcha-garden --headless --frames 120 --capture garden.png
```

The original `dmg`, `netplay`, Gym library and benchmark remain headless tools;
adding a desktop player does not make their existing CLI invocations graphical.
A Linux system can still build those tools alone with
`-DMATCHA_BUILD_AUTOPSY=OFF`.
