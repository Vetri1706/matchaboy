# Matchaboy on Windows

The Windows player uses the same game library, controller mapping model,
GB/GBA cores and two-player friend-session protocol as the Mac player. Its
windows, menus, settings and connection dialogs use Win32 controls. It does
not require a separate mGBA installation, SDL, Qt, or a downloaded runtime.

This revision adds Windows keyboard settings and interactive Host/Join. Strict
MinGW cross-compilation is a build check, not Windows runtime verification.
Native dialog, player and network tests must pass in the Windows workflow before
a package from this revision is treated as verified. A successful older artifact
does not contain these changes.

## Open a game

Extract the Windows ZIP and open **Matchaboy.exe**. Keep its accompanying
files together. The library contains licensed Tobu Tobu Girl homebrew releases; select one and
choose **Play**. **File > Open game…** (Ctrl+O) opens your own `.gb` or `.gba`
cartridge. **File > Game library** (Ctrl+L) returns to the library. Cancelling
an open dialog keeps the current game.

GB and GBA cartridge progress is stored beside the ROM as
`<game>.matchaboy.sav`; use a writable folder and make an ordinary in-game save.
The application does not overwrite another emulator's `.sav` file.

## Keyboard settings

Open **Tools > Keyboard Settings…** or press **Ctrl+,**. The player guide also
has a **Keyboard Settings…** button. Click a binding and press its replacement,
or use **Set all keys…** to assign every button in sequence.

| Console control | Balanced default |
| --- | --- |
| D-pad up / left / down / right | W / A / S / D |
| A / B | L / K |
| L / R shoulders (GBA) | Q / I |
| Start / Select | Enter / Space |

**Apply** saves the complete mapping for your Windows user. **Cancel** keeps the
previous mapping. **Balanced** restores the table above; **Classic** restores
arrows, Z/X, Shift for Select, Enter for Start and Q/W for shoulders. Both Enter
keys are aliases; both Shift keys are aliases. Bindings describe physical ANSI
key positions, independent of the active keyboard input language.

Escape, Tab and F12 are reserved for emulator controls. Duplicate or unsupported
keys show a correction message and disable Apply. Escape cancels an active key
capture first; Escape again closes settings. Click Cancel to close immediately.
Preferences live only in Matchaboy's per-user registry value
`HKCU\Software\Matchaboy\KeyboardMappingV1`; the app never remaps Windows itself.

The guide always shows your current **game button → key** assignments and marks
held keys with `*`. Space is **Select**, not fast-forward. Settings pause local
play, then restore its previous state. Linked play continues while settings is
open; neither player receives input from the settings editor.

| Emulator action | Shortcut |
| --- | --- |
| Pause / resume local play | Escape |
| Show / hide controller guide | Ctrl+Shift+C |
| Mute / unmute | Ctrl+Shift+M |
| Game Boy palette | Ctrl+P |
| Toggle Inspector | Tab |
| Inspector Video / CPU / Memory / Audio | Ctrl+1 / 2 / 3 / 4 |
| Step instruction / frame / GB peripheral dot | Ctrl+S / F / D |
| Screenshot and diagnostic files | F12 |

Dot stepping is not available for GBA. Stepping and local pause are disabled
while linked. Plain letter keys belong to the game, including WASD while
Inspector is visible.

## Play with a friend

Open the exact same ROM revision in matching builds on both computers. Use
**Netplay > Host game…** on one computer and **Netplay > Join game…** on the
other. Share the host's reachable IPv4 address, UDP port (default 27888) and
room code. Each person can choose a different keyboard mapping. See
[NETPLAY.md](NETPLAY.md) for connection steps, save handling and compatibility.

**Netplay > Connection Details…** has separate Copy room code and Copy
diagnostics actions. The settings and connection dialogs continue dispatching
the player's timer messages, so opening a dialog does not deliberately suspend
an active link. A ten-second loss of valid peer traffic still ends a session;
this does not provide automatic reconnection or diagnose the previous Mac drop.

Internet play needs a reachable VPN address or host UDP forwarding. The app
has no relay, matchmaking, automatic router setup or encrypted transport.

## Build and verify

Use an x64 Native Tools Command Prompt with LLVM, CMake, Ninja and the Windows
SDK installed:

```bat
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release -DMATCHA_BUILD_AUTOPSY=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

Release builds enforce C++20 and strict warnings. The native keyboard presenter
test drives real Win32 controls and its message queue, checks capture, presets,
Apply/Cancel, validation, alias keys, timer delivery and quit handling. Registry
checks redirect HKCU inside the test process to a unique temporary test key;
they do not change a person's existing Matchaboy preferences.

Windows audio uses WinMM and the native player uses OpenGL/GDI+. If no audio
device is available, emulation continues silently. The graphics-processor menu
is Windows-specific and changes only this executable's Windows GPU preference.
The [download guide](DOWNLOADS.md) describes package verification and notices.
