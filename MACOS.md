# Matchaboy for Mac

The native Mac app now opens the same original ten-game arcade as Windows.
Choose a cartridge and **Play game**, or use **Game > Open Game** to load a
`.gb` or `.gba` file. Games open in the large player view; **Tab** opens the
optional Video, CPU, Memory and Audio Inspector.

## Build and open

```sh
cmake -S . -B build/mac -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++
cmake --build build/mac --parallel 4
open build/mac/MatchaAutopsy.app
```

The bundle displays as **Matchaboy**. Its historical on-disk filename is retained
for existing build and package scripts. The `.app` contains all ten original
cartridges and can be moved independently of the checkout. `make platform` also
builds this complete app. Native frameworks handle the window, graphics and
48 kHz stereo playback; no separate emulator, audio package or BIOS is required.

Game Boy uses the original Matchaboy core. GBA uses the same statically bundled
mGBA bridge as the Windows player. See [THIRD_PARTY.md](THIRD_PARTY.md) for its
source and notices. Download packages include corresponding `source.zip` and
licenses alongside the app.

## Controls

| Action | Mac key |
| --- | --- |
| Select a cartridge | Arrow keys |
| Play selected cartridge / Start in game | Enter |
| D-pad | Arrow keys |
| A / B | Z / X |
| Select | Shift |
| GBA L / R shoulders | Q / W |
| Pause / resume | Space |
| Show / hide Inspector | Tab |
| Inspector Video / CPU / Memory / Audio | 1 / 2 / 3 / 4 |
| Inspector instruction / frame / GB dot | S / F / D |
| Open ROM / game library | Command-O / Command-L |
| Mute / show controls | M / C |

The menus expose player actions too. GB grayscale/green selection does not
restart the cartridge. GBA dot stepping is unavailable; its CPU view shows
registers and raw instruction memory, not an invented retired-instruction log.

Audio pauses when browsing the library, when paused/muted, and on focus loss.
Playback uses the default Mac output device at 50% application gain without
changing system volume. A missing device leaves the game usable silently.

Bundled games are prepared under this user's
`~/Library/Application Support/Matchaboy/Library`. The app never writes inside
its bundle. GBA saves use `<game>.matchaboy.sav` beside the ROM on normal close
or game switch; choose a writable folder for external games. Existing `.sav`
files are not overwritten.

## Verify the Mac build

```sh
ctest --test-dir build/mac --output-on-failure
python3 tools/test_player_macos.py --binary build/mac/MatchaAutopsy.app \
  --dmg build/mac/dmg --output artifacts/mac-player
build/mac/macos_audio_probe games/gb/roms/moon-courier.gb
build/mac/macos_audio_probe games/gba/roms/drift-circuit.gba
```

The player checks execute real cartridges from a relocated app, compare LCD
pixels, inspect both machines, exercise input and GBA save/reload, and reject
invalid files/options. Add `--window-tests` for real native GPU readbacks.
The audio probes require an actual Mac output device and check submitted and
recycled Audio Queue packets plus pause/reset behavior. They do not prove
subjective listening quality or long-term device-switch behavior.

Builds are not Apple-notarized. Game Boy Color-only cartridges and interactive
GBA netplay remain unsupported. The Windows GPU-preference menu is intentionally
absent: the Mac app uses its native system graphics context.
