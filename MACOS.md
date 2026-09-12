# Matchaboy for Mac

The native Mac app now opens the same licensed homebrew library as Windows and Linux.
Choose a cartridge and **Play game**, or use **Game > Open Game** to load a
`.gb` or `.gba` file. Games open in the large player view; **Tab** opens the
optional Video, CPU, Memory and Audio Inspector.

The replacement Apple Silicon download targets **macOS 13 or later**. Earlier
host-built packages accidentally required macOS 26. The deployment target is a
build compatibility setting; execution on macOS 13 has not been physically
verified.

## Open the download

Extract the Mac ZIP and double-click **Matchaboy.app** to play. You can also
drag the app to **Applications**. The download contains three items:

- **Matchaboy.app** — the player, with both Tobu Tobu Girl releases and their full licenses inside.
- **Start Here.txt** — opening instructions and controls.
- **Extras** — optional command-line/Gym tools, documentation, source and licenses.

The player runs independently of `Extras`. Developers can open Terminal in the
download folder and use `cd Extras` before running `./dmg`, `./gym_benchmark`, or
Python with `matcha_gym.py`. Build details and file hashes are in
`Extras/BUILD_INFO.json`; corresponding source is in `Extras/source.zip` and
notices are in `Extras/licenses` and `Extras/THIRD_PARTY.md`.

## Build and open

```sh
cmake -S . -B build/mac -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++
cmake --build build/mac --parallel 4
open build/mac/MatchaAutopsy.app
```

Fresh Mac build directories default to a macOS 13.0 deployment target. To update
an existing directory or choose another target explicitly, add
`-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0` to the configure command, then rebuild.

The build produces `MatchaAutopsy.app`; packaging names the download
`Matchaboy.app`. Both display as **Matchaboy**. The `.app` contains the bundled homebrew
cartridges and full license notices and can be moved independently of the checkout. `make platform` also
builds this complete app. Native frameworks handle the window, graphics and
48 kHz stereo playback; no separate emulator, audio package or BIOS is required.

Game Boy uses the original Matchaboy core. GBA uses the same statically bundled
mGBA bridge as the Windows player. See [THIRD_PARTY.md](THIRD_PARTY.md) for its
source and notices. Download packages include corresponding `source.zip` and
licenses in `Extras`.

## Downloaded app and macOS security

The replacement Mac ZIP signs the complete `.app` after installing its resources.
Packaging verifies that signature before archiving and after extracting the ZIP.
The earlier netplay ZIP omitted the bundle's resource seal, which could produce
“App is damaged” on another Mac. Replace that copy with the rebuilt ZIP.

This is an **ad-hoc signature**, which checks bundle integrity. It does not
identify a verified developer or provide Apple notarization; no Developer ID
identity is configured for these builds. macOS may still block a downloaded app.
If you trust the replacement and macOS reports an unverified developer, first try
opening it, then use **System Settings > Privacy & Security > Open Anyway** if
offered. See [Apple's instructions](https://support.apple.com/102445).

To check the download, place the ZIP and its matching `.sha256` file in the same
directory and run this command from that directory:

```sh
shasum -a 256 -c matchaboy-macos-arm64.zip.sha256
codesign --verify --deep --strict --verbose=2 /full/path/to/Matchaboy.app
```

For the second command, replace the path with the extracted app's full path, or
type the command through `--verbose=2 ` and drag the app into Terminal before
pressing Return. A successful `codesign` check confirms intact signed contents,
not Gatekeeper approval. If either check fails, obtain the replacement archive
again; do not treat a damaged signature as an ordinary trust prompt. Automatic
Gatekeeper approval requires the separate [Developer ID and notarization
process](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution).

## Controls

The **Balanced** preset is the default. When a game asks for **A**, press **L**;
when it asks for **B**, press **K**. These are console buttons; the game's own
controls determine whether they pass, shoot, jump, or perform another action.

| Action | Mac key (Balanced) |
| --- | --- |
| Select a cartridge | Arrow keys |
| Play selected cartridge / Start in game | Enter |
| D-pad up / left / down / right | W / A / S / D |
| A / B | L / K |
| Select | Space |
| GBA L / R shoulders | Q / I |
| Pause / resume | Escape |
| Show / hide Inspector | Tab |
| Inspector Video / CPU / Memory / Audio | Command-1 / 2 / 3 / 4 |
| Inspector instruction / frame / GB dot | Command-S / Command-F / Command-D |
| Open ROM / game library | Command-O / Command-L |
| Mute / show controls | Command-Shift-M / Command-Shift-C |
| Game Boy green palette | Command-P |
| Save screenshot | F12 or Command-Shift-S |
| Keyboard Settings | Command-Comma |

**Space is Select, not fast-forward or pause.** Escape pauses local play; linked
play continues until disconnected. The guide reads **Game button → Press key**,
uses your active mapping, and marks locally held keys with `*`.

Open **Matchaboy > Keyboard Settings…** or press **Command-Comma** to change the
game keys. Choose **Balanced** or **Classic**, click an individual binding and
press its replacement key, or use **Set all keys…** to capture every binding in turn.
**Apply** saves the validated mapping for future launches; **Cancel** keeps the
previous mapping. Duplicate and reserved keys receive a correction message.
Changing settings does not reset the game or change network input timing.

**Classic** restores arrows for the D-pad, Z for A, X for B, Shift for Select,
Enter for Start, and Q/W for shoulders.
The emulator shortcuts above still apply. Bindings use physical ANSI keyboard
positions, with ANSI key names in the guide; they do not follow text produced by
an input method. Both Shift keys and Return/keypad Enter share their bindings.

The menus expose player actions too. GB grayscale/green selection does not
restart the cartridge. GBA dot stepping is unavailable; its CPU view shows
registers and raw instruction memory, not an invented retired-instruction log.

Audio pauses when browsing the library or when paused/muted. Local play also pauses on focus loss; linked play clears held buttons and continues running.
Playback uses the default Mac output device at 50% application gain without
changing system volume. A missing device leaves the game usable silently.

Bundled games are prepared under this user's
`~/Library/Application Support/Matchaboy/Library`. The app never writes inside
its bundle. Battery-backed Game Boy and GBA saves use `<game>.matchaboy.sav` beside the ROM on normal close
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

Game Boy Color-only cartridges remain unsupported. The Windows GPU-preference menu is intentionally
absent: the Mac app uses its native system graphics context.

## Play with a friend

1. Open the same link-capable ROM version on both computers.
2. On one Mac, choose **Netplay > Host Game**. Keep the UDP port (27888 by
   default), copy the generated room code, and choose **Host game**.
3. On the other Mac, choose **Netplay > Join Game**. Enter the host's IP/name,
   UDP port, and the complete room code, then choose **Join game**.
4. Both games restart from their saved progress. Use the cartridge's own link
   cable or multiplayer menu. The footer shows player number, linked frame, and
   whether the game is waiting for the other player.

On the same Wi-Fi, use the host's LAN IP; `127.0.0.1` is only for two instances
on one computer. Internet play requires a reachable VPN address or forwarding
that UDP port on the host's router. Matchaboy does not configure routers/firewalls,
provide a public relay, or encrypt room codes and game data. Use a trusted VPN
for a private connection. Each player still needs their own legitimate ROM.

Game Boy and GBA sessions connect two actual emulated consoles. They exchange
initial saved progress, confirmed inputs, and state hashes; simulation waits for
missing peer input instead of inventing it. A six-frame input delay absorbs some
network jitter. This does not add multiplayer to single-player games or promise
compatibility with every commercial link mode.

**Netplay > Connection Details** shows the full status and offers **Copy room
code** and **Copy diagnostics**. If a session fails, copy diagnostics **before
Disconnect**. They include the current and last verified frame, successful send
and accepted receive counts, retransmissions, and time without peer traffic.
They exclude the room code, address, ROM path, and save data.
**Disconnect** detaches the cable, saves only your local console's progress, and
returns to local play. During the session, pause, hardware stepping, opening a
ROM, and the library are disabled. Inspector tabs, palette, sound, controls, and
screenshots remain available. Focus loss releases buttons without pausing the
connection. A failed session stays visible until disconnected.

The app suppresses App Nap while a friend session is unfinished and continues
servicing the connection during its dialogs. Normal system sleep is still
allowed. Three-second packet blackouts and pauses in peer polling recover in
real-socket tests; ten seconds without valid peer traffic still ends the
connection, with no automatic reconnect. These changes improve resilience and
diagnostics; the cause of the user's reported mid-session disconnect is unknown.

For headless integration checks, launch two processes from the extracted download
folder with the same ROM and code:

```sh
./Matchaboy.app/Contents/MacOS/MatchaAutopsy game.gb --net-host 27888 \
  --net-code 0123456789abcdef0123456789abcdef --headless --frames 120 --capture host.png
./Matchaboy.app/Contents/MacOS/MatchaAutopsy game.gb --net-join 127.0.0.1:27888 \
  --net-code 0123456789abcdef0123456789abcdef --headless --frames 120 --capture join.png
```

Run these in separate terminals. Headless capture waits up to 60 seconds for the
requested linked frames, returns an error on failure, and records connection
status/role/frame in the capture JSON. It never records the room code. The sample
code is for local testing; generate a fresh code in the Host Game dialog for play.

A coordinating test harness may pass `--net-stop-file PATH` to both headless
processes. Each prints `Friend capture ready` at the requested frame, then services
network ACKs and state checks without advancing until that file exists (up to
10 seconds). The capture records `verified_frames` and the actual final connection
state, including a peer that disconnected after completing its requested frames.
