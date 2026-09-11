# Download and build Matchaboy

The **Build and Package Binaries** workflow builds Windows x64 with native
`clang++`, Linux x64 with Clang, and macOS Apple Silicon with Apple Clang.
Every package is uploaded only after its native tests, original ROM checks,
Gymnasium tests, real UDP replay, and extracted-package smoke tests pass.

## Download from GitHub

1. Open [Actions](https://github.com/Vetri1706/matchaboy/actions/workflows/build.yml).
2. Select a successful run, then its **Artifacts** section.
3. Download `matchaboy-windows-x64`, `matchaboy-linux-x64`, or
   `matchaboy-macos-arm64`.
4. Extract the artifact, then the included platform ZIP. Keep the files together.

Each ZIP includes the headless emulator, UDP netplay runner, Gym benchmark,
`libmatcha` shared library, Python wrapper, C header, and documentation. macOS
also includes `MatchaAutopsy.app`; Windows includes `Matchaboy.exe`, the native
Matchaboy Inspector dashboard. `BUILD_INFO.json` records the source commit,
compiler, test results and file hashes; the adjacent `.sha256` file checks the
whole ZIP. Current Windows builds embed the ten original games under `games/`;
their corresponding sources and redistribution notices accompany the player.
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

Linux/macOS, from the extracted directory:

```sh
./dmg /path/to/game.gb --frames 120 --frame-out screen.pgm --report run.json
./gym_benchmark /path/to/game.gb --instances 16 --seconds 10
```

`dmg` runs headlessly; it does not open a game window. The benchmark retains its
50,000 aggregate FPS target and reports failure when the machine misses it.
The native hardware dashboard is available on Windows and macOS. Linux remains
headless. On Windows, double-click `Matchaboy.exe` to open the original arcade,
select a game and Play. Use Open game to choose your own ROM, or run:

```powershell
.\Matchaboy.exe C:\roms\game.gb
```

On macOS:

```sh
./MatchaAutopsy.app/Contents/MacOS/MatchaAutopsy /path/to/game.gb --paused
```

In the Windows player, opening a ROM shows a clean view with a large LCD and optional keyboard
controls. **Open game...** or **File > Open game** (Ctrl+O) loads a `.gb` or `.gba`
ROM in the same window. Cancelling preserves the current game. **Ctrl+L** or
**File > Game library** returns to the ten-game catalog and pauses the game.
The library shows each game's goal, controls and an Inspector reading guide.
Color-only ROMs receive a clear unsupported-format message.
**Inspector** or Tab toggles the hardware panels without restarting the game.
Use `--inspector` to start directly in that view.

The Windows player uses grayscale by default. **Audio/Video > Player palette** offers
grayscale or original green, without restarting the game. The game texture uses
nearest-neighbor scaling and a hardware-rate frame schedule (about 59.73 fps).
The Inspector retains its diagnostic colors.

The optional Inspector preserves the address heatmap, LCD, live FIFO/fetcher,
registers, instruction history and four digital APU scopes. Space pauses;
S/F/D step one instruction/frame/peripheral dot in Inspector. Scroll moves through the trace.
Arrows, Z/X, Enter and Shift supply joypad input. F12 saves the displayed OpenGL
viewport to `autopsy-capture.png` and its hardware-state JSON in the working
directory. Use `--capture PATH` to select another capture destination. The
scopes inspect digital outputs. The Windows player outputs 48 kHz stereo through Windows itself; M or Audio > Sound on toggles playback.

These builds are not signed with a paid Windows publisher certificate or
notarized with an Apple Developer identity. Operating-system trust prompts can
therefore apply to downloaded executables.

The optional Python interface requires NumPy and Gymnasium:

```sh
python -m pip install numpy==2.4.6 gymnasium==1.3.0
```

Run Python beside `matcha_gym.py` and its `libmatcha.dll`, `.dylib` or `.so`.
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
is required. `MATCHA_BUILD_AUTOPSY` defaults to ON on Windows/macOS. For an
existing build directory previously configured with it disabled, add
`-DMATCHA_BUILD_AUTOPSY=ON` to the configure command.
Networking uses actual nonblocking Winsock sockets, and benchmark memory
measurement uses the Windows process API. GNU-driver Clang uses
`-std=c++20 -O3 -Wall -Wextra -Werror -Wpedantic`.

The same CMake commands work with Clang on Linux/macOS. The original Makefile
remains available on POSIX systems. WSL uses the Linux build and produces Linux
binaries; it does not produce a native Windows `.exe`.

## Verification scope

CI runs eight native suites, four Python/Gymnasium tests, 131 selected original
ROM executions, seven adversarial UDP protocol cases, and real two-process
UDP replay with 100 ms one-way delay and 5% loss for 100 frames plus an idle-peer
case. It also unpacks each ZIP into a different directory containing spaces,
runs the packaged emulator, and steps 16 real VMs through the packaged DLL.
Both Windows and macOS packaged dashboards must produce a real headless PNG.
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

## Portable Windows GBA player

The Windows player accepts `.gb` and `.gba` files. Game Boy games use the
original Matchaboy engine; GBA games use the statically bundled mGBA 0.10.5 core.
No separate emulator, external BIOS, Qt, SDL or runtime installation is needed.
GBA adds Q/W for L/R shoulder buttons. Tab opens its Video, CPU, Memory and Audio
Inspector panels. S/F step instructions/frames; memory pages use PgUp/PgDn or
the mouse wheel. The player supports speaker playback; GBA netplay is not implemented.

GBA cartridge saves use `<game>.matchaboy.sav` beside the ROM, flushed on normal
close or game switch. Keep games in a writable folder. Existing mGBA `.sav`
files are not overwritten. Source and license notices accompany portable builds;
see THIRD_PARTY.md. To build explicitly select both `clang` for C and `clang++`
for C++, with the static MSVC runtime already configured by CMake.

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
