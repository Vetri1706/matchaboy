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
also includes `MatchaAutopsy.app`. `BUILD_INFO.json` records the source commit,
compiler, test results and file hashes; the adjacent `.sha256` file checks the
whole ZIP. Source ROMs, commercial games and Python packages are not bundled.

The repository is private: downloads and releases require repository access.
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
The existing native hardware dashboard uses macOS system frameworks and is
not included in the Windows or Linux packages. On macOS:

```sh
./MatchaAutopsy.app/Contents/MacOS/MatchaAutopsy /path/to/game.gb --paused
```

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
The macOS packaged dashboard must produce a real headless PNG.

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
