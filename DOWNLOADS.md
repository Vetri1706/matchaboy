# Download and build Matchaboy

The **Build and Package Binaries** workflow builds Windows x64 with native
`clang++`, Linux x64 with Clang, and macOS Apple Silicon with Apple Clang.
The workflow gates packages on native tests, original ROM checks, Gymnasium
tests, real UDP replay, and extracted-package smoke tests. The Windows
settings/netplay and Linux desktop changes in this revision still await native
CI evidence; use a successful run containing these changes, not an older ZIP.

## Download from GitHub

1. Open [Actions](https://github.com/Vetri1706/matchaboy/actions/workflows/build.yml).
2. Select a successful run, then its **Artifacts** section.
3. Download `matchaboy-windows-x64`, `matchaboy-linux-x64`, or
   `matchaboy-macos-arm64`.
4. Extract the artifact, then the included platform ZIP. Keep the files together.

On Mac, open **Matchaboy.app** to play. The extracted folder contains only the
app, **Start Here.txt**, and **Extras**. `Start Here.txt` explains opening and
controls; `Extras` holds optional developer tools, documentation, source and
licenses. The app also works after dragging it to **Applications**.

Each ZIP includes the headless emulator, UDP netplay runner, Gym benchmark,
`libmatcha` shared library, Python wrapper, C header, and documentation; these
are in `Extras` on Mac. Windows includes `Matchaboy.exe`; Linux includes the
`Matchaboy` desktop executable. The players include the ten original games and
optional GB/GBA Inspector. Linux needs its X11/Xft system libraries and optionally
ALSA for sound; see [LINUX.md](LINUX.md).
`BUILD_INFO.json` (`Extras/BUILD_INFO.json` on Mac) records the source commit,
compiler, test results and file hashes; the adjacent `.sha256` file checks the
whole ZIP. Corresponding source and redistribution notices accompany the players;
on Mac, see `Extras/source.zip`, `Extras/licenses`, and `Extras/THIRD_PARTY.md`.
Commercial games and Python packages are not bundled.

The repository is public; Actions artifact downloads require GitHub sign-in.
Artifacts are retained for 30 days. Pushing a `v*` tag creates or updates a
release with the three tested ZIPs and checksums after every build succeeds.
Releases inherit the repository's visibility; the workflow does not make them
public. The workflow also supports manual **Run workflow** execution.

## Run the downloaded files

Windows 10/11 x64, in PowerShell inside the extracted directory:

```powershell
.\dmg.exe C:\roms\game.gb --frames 120 --frame-out screen.pgm --report run.json
.\gym_benchmark.exe C:\roms\game.gb --instances 16 --seconds 10
```

Linux, from the extracted directory:

```sh
./dmg /path/to/game.gb --frames 120 --frame-out screen.pgm --report run.json
./gym_benchmark /path/to/game.gb --instances 16 --seconds 10
```

Mac developer tools, from the extracted download folder:

```sh
cd Extras
./dmg /path/to/game.gb --frames 120 --frame-out screen.pgm --report run.json
./gym_benchmark /path/to/game.gb --instances 16 --seconds 10
```

`dmg` runs headlessly; it does not open a game window. The benchmark retains its
50,000 aggregate FPS target and reports failure when the machine misses it.
The native players are available on Windows, macOS and Linux.
Open `Matchaboy.exe` on Windows, `Matchaboy.app` on Mac, or run `./Matchaboy`
on Linux in a graphical X11/XWayland session to open the
original arcade, select a game and Play. Use Open game to choose your own ROM,
or run:

```powershell
.\Matchaboy.exe C:\roms\game.gb
```

On macOS, from the extracted download folder:

The replacement Apple Silicon package targets macOS 13 or later; earlier
host-built packages accidentally required macOS 26. This is a deployment-target
compatibility setting, not a claim of runtime testing on macOS 13.

```sh
open Matchaboy.app
./Matchaboy.app/Contents/MacOS/MatchaAutopsy /path/to/game.gb --paused
```

The [Mac guide](MACOS.md), [Windows guide](WINDOWS.md) and [Linux guide](LINUX.md)
cover each player, its menus, audio and controls.
If an earlier Mac netplay download reports “App is damaged,” replace it with the
rebuilt ZIP: the old bundle lacked a resource seal. Current Mac packages apply an
ad-hoc signature after installing all app resources and verify it before and
after ZIP extraction. They are not Developer ID-signed or Apple-notarized, so a
trusted replacement may still need **System Settings > Privacy & Security >
Open Anyway** after the first blocked launch. See the [Mac security and checksum
instructions](MACOS.md#downloaded-app-and-macos-security) before overriding a prompt.
In each player, opening a ROM shows a clean view with a large LCD and optional
keyboard controls. **Open game...** loads a `.gb` or `.gba` ROM in the same
window: **File > Open game** (Ctrl+O) on Windows, **Game > Open Game** (Command-O)
on Mac. Cancelling preserves the current game. **File > Game library** on
Windows or **Game > Library** on Mac returns to the ten-game catalog and pauses
the game (Ctrl+L on Windows, Command-L on Mac).
The library shows each game's goal, controls and an Inspector reading guide.
Color-only ROMs receive a clear unsupported-format message.
**Inspector** or Tab toggles the hardware panels without restarting the game.
Use `--inspector` to start directly in that view.

The players use grayscale by default. Windows **Audio/Video** palette options
and Mac **View > Game Boy Green Palette** switch grayscale/green without
restarting the game. The game texture uses
nearest-neighbor scaling and a hardware-rate frame schedule (about 59.73 fps).
The Inspector retains its diagnostic colors.

The optional Inspector exposes actual memory, LCD, registers and audio data.
The default **Balanced** preset on all three players uses WASD for the D-pad,
L for A, K for B, Q/I for GBA shoulders, Enter for Start and Space for Select.
**Space is not fast-forward. Escape pauses local play.** Open **Keyboard
Settings…** from **Tools** on Windows/Linux (Ctrl-Comma) or the **Matchaboy**
menu on Mac (Command-Comma). Capture individual keys, use **Set all keys…**,
or choose Balanced/Classic. Apply saves your mapping; Cancel keeps the previous
one. Classic restores arrows/Z/X/Shift and Q/W shoulders. The game guide follows
your active mapping. Friends may use different layouts.

Game bindings use physical ANSI key positions. Windows/Linux use Ctrl for
application shortcuts; Mac uses Command. The modifier plus Shift-C toggles the
guide, Shift-M toggles sound, and P switches the GB palette. Tab opens Inspector;
modifier-S/F steps instructions/frames. Windows/Mac also support modifier-D
for a GB peripheral dot and modifier-1–4 for separate panels; Linux currently
shows a combined CPU/FIFO/memory inspector. Unmodified letter keys stay
available to the game. Platform-specific details and
current verification status are in the three player guides.

F12 saves a displayed-frame capture and hardware-state JSON. Windows writes `autopsy-capture.png` in the
working directory; Mac writes a timestamped `~/Pictures/Matchaboy-*.png` and
also offers Command-Shift-S or **Game > Save Screenshot**. Use `--capture PATH`
to select another capture destination. The
scopes inspect digital outputs. The players output 48 kHz stereo through their
operating-system audio APIs when a device is available; Linux audio is optional
at build time. Ctrl-Shift-M toggles playback on Windows/Linux;
Command-Shift-M does so on Mac. The menu is
**Audio/Video > Sound on** on Windows and **Game > Sound** on Mac.

Windows builds have no publisher certificate. Mac ad-hoc signing verifies
integrity but does not establish developer identity or notarization.
Operating-system trust prompts can therefore apply to downloaded executables.

The optional Python interface requires NumPy and Gymnasium:

```sh
python -m pip install numpy==2.4.6 gymnasium==1.3.0
```

Run Python beside `matcha_gym.py` and its `libmatcha.dll`, `.dylib` or `.so`.
On Mac, use `cd Extras` from the download folder first.
The wrapper also accepts `NativeBatch(..., library="/explicit/library/path")`
or the `MATCHA_LIBRARY` environment variable. Borrowed-memory contracts are
documented in [PLATFORM.md](PLATFORM.md).

## Build native Windows with clang++

Install LLVM, CMake, Ninja, and the Visual Studio C++ Build Tools/Windows SDK.
Open an **x64 Native Tools Command Prompt for Visual Studio**, then run:

```bat
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
cmake --install build --prefix stage
```

Clang is the compiler; Microsoft's headers and system libraries provide the
native Windows SDK and C++ runtime. Release builds use the static MSVC runtime.
The dashboard uses Win32, GDI+ and OpenGL from Windows; no external GUI library
is required. `MATCHA_BUILD_AUTOPSY` enables the desktop player; Linux requires the X11/Xft
development package when it is enabled. For an
existing build directory previously configured with it disabled, add
`-DMATCHA_BUILD_AUTOPSY=ON` to the configure command.
Networking uses actual nonblocking Winsock sockets, and benchmark memory
measurement uses the Windows process API. GNU-driver Clang uses
`-std=c++20 -O3 -Wall -Wextra -Werror -Wpedantic`.

The same CMake commands work with Clang on Linux/macOS after installing the
platform development dependencies; see [LINUX.md](LINUX.md) for X11/Xft/ALSA. The original Makefile
remains available on POSIX systems; its Mac `platform` target delegates the
complete native app's dependency graph to CMake. WSL uses the Linux build and produces Linux
binaries; it does not produce a native Windows `.exe`.

## Verification scope

The verification workflow runs native suites, Python/Gymnasium tests, 131 selected original
ROM executions, seven adversarial UDP protocol cases, and real two-process
UDP replay with 100 ms one-way delay and 5% loss for 100 frames plus an idle-peer
case. It also unpacks each ZIP into a different directory containing spaces,
runs the packaged emulator, and steps 16 real VMs through the packaged DLL.
Windows and macOS packaged players must produce a real headless PNG. Linux
adds desktop-window checks under Xvfb and actual LCD headless captures; those
new checks must pass on the Linux runner before their runtime results are claimed.
Mac additionally executes all ten bundled cartridges from a relocated app,
checks title/gameplay LCD pixels, compares GB output with separate core CLI
runs, inspects both machines, and verifies GBA hardware input and save/reload.
Optional local native GPU parity checks use the app's `--window-test` capture mode. See
[MACOS.md](MACOS.md) for local commands and separately measured speaker playback.
Windows additionally checks native OpenGL readback against the headless render
at the same emulated state and exercises its real window message handlers for
pause, stepping, joypad, trace scroll, resizing, capture and closing. Run those
checks locally with:

```powershell
python tools/verify_autopsy.py --binary build/Matchaboy.exe --rom roms/homebrew/silicon_audio.gb --output artifacts/windows-hud
python tools/test_autopsy_windows.py --binary build/Matchaboy.exe --output artifacts/windows-controls
```

Generate that demo ROM first with `python tools/make_audio_demo.py`. Each check
requires a fresh output directory. GUI checks require an interactive Windows
desktop; headless capture does not require a window.

This does not mean every upstream hardware test passes. Hardware exclusions
and unverified commercial-ROM compatibility remain as documented. Older
POSIX-specific orchestration (`make`, `resource`/`RLIMIT_FSIZE`, signals, and
process groups in `verify_platform.py`/`benchmark_host.py`) is not a native
Windows workflow; use CMake and `.github/scripts/verify_build.py` there.

The frozen M3 comparison in [PLATFORM_VERIFICATION.md](PLATFORM_VERIFICATION.md)
predates the portability changes. Check out commit `f25eb24` or its preserved
source archive to reproduce that exact source inventory. Record a new baseline
for performance comparisons of the current portable revision; never compare
different source inventories as if they were the same workload.

Official references: [artifact downloads](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/download-workflow-artifacts),
[hosted runner platforms](https://docs.github.com/en/actions/reference/runners/github-hosted-runners),
and the [Windows runner toolchain](https://github.com/actions/runner-images/blob/main/images/windows/Windows2022-Readme.md).

## Native GB/GBA players

The Windows, Mac and Linux players accept `.gb` and `.gba` files. Game Boy games use the
original Matchaboy engine; GBA games use the statically bundled mGBA 0.10.5 core.
No separate emulator, external BIOS, Qt or SDL is needed. Linux uses X11/Xft and
optional ALSA system libraries; Windows and Mac use their platform frameworks.
Balanced maps GBA L/R shoulders to Q/I. Tab opens Inspector; modifier-S/F steps
instructions/frames. The players expose interactive GB/GBA cable Host/Join
using the shared FriendSession protocol; see [NETPLAY.md](NETPLAY.md). Native
Windows/Linux and mixed-platform gameplay evidence is tracked separately from
the existing Mac tests.

GBA cartridge saves use `<game>.matchaboy.sav` beside the ROM, flushed on normal
close or game switch. Keep games in a writable folder. The Mac arcade prepares
its writable games under `~/Library/Application Support/Matchaboy/Library`,
leaving the app bundle unchanged. Existing mGBA `.sav`
files are not overwritten. Source and license notices accompany portable builds;
see THIRD_PARTY.md. To build explicitly select both `clang` for C and `clang++`
for C++, with the static MSVC runtime already configured by CMake on Windows.

`python tools/test_gba_windows.py --binary build/Matchaboy.exe --output artifacts/gba`
checks an authored ARM ROM, actual A/L/R hardware input, and save/reload from
an isolated folder with a system-only PATH. This is not a clean Windows VM test.

Windows sound uses built-in WinMM at 48 kHz stereo with a short buffer queue.
Pause, mute and opening another game clear queued audio. Application gain is
50%; the system volume is never changed. Inspector tracing is collected while
inspecting; normal play skips that instrumentation. If no output device is
available, the game continues silently and the Audio menu reports it.
`test_audio_windows.py --require-device` verifies nonzero GB/GBA PCM and actual
Windows buffer consumption, plus pause/mute. Without that flag CI records device
availability and still checks PCM generation. F12 writes `.audio.json` diagnostics.
Mac playback uses the system Audio Queue API with completed-buffer telemetry;
its device probes are described in [MACOS.md](MACOS.md).
