# Matcha platform

Matcha extends the existing DMG CPU and pixel-FIFO core with deterministic
snapshots, serial rollback over real UDP, native hardware instrumentation, and
independent threaded learning environments. Core and network code use C++20,
the standard library and operating-system sockets. The optional macOS player
uses system AppKit/OpenGL and AudioToolbox frameworks; the Windows player uses
Win32/GDI+/OpenGL and WinMM. The Linux desktop uses X11 and optional ALSA.
These platform libraries do not change the independent headless DMG core.
The optional Python adapter uses ctypes,
NumPy, and Gymnasium; none is linked into the emulator.

Measured results, retained evidence, unmet performance targets, and the exact
future-host comparison prompt are in [PLATFORM_VERIFICATION.md](PLATFORM_VERIFICATION.md).

## Build and run

```sh
make -j4 platform
make platform-test
make platform-sanitize
python3 tools/verify_all.py --output artifacts/my-rom-regression
```

Every C++ target uses strict warnings. Release targets use `-O3`; the optional
`build/gym_benchmark_lto` adds link-time optimization. The desktop player supports
macOS, Windows and Linux through CMake; the headless core, telemetry tests, UDP
peers and Gym remain available separately. See [DOWNLOADS.md](DOWNLOADS.md) and
the [Mac](MACOS.md), [Windows](WINDOWS.md) and [Linux](LINUX.md) guides. Each
player statically bundles mGBA for GBA games; the original DMG core remains
independent. Linux requires X11, with optional ALSA for sound. See THIRD_PARTY.md.
The Windows keyboard/netplay and Linux desktop changes await native CI evidence
for this revision; cross-compilation is not a runtime pass.

## Snapshots

`save_snapshot(cpu, bus, snapshot)` and `load_snapshot(cpu, bus, snapshot)` use
a fixed preallocated `MachineSnapshot`. Call them between CPU instructions,
outside any active bus operation. They explicitly serialize registers,
prefetch and interrupt state, memory, timer reload phases, DMA startup/phase,
joypad, serial bit position, MBC/RTC, the complete pixel fetcher/FIFOs and
framebuffer, and APU channel/mixer/sample-buffer state. Cartridge ROM is
identified by SHA-256 and shared separately. Socket endpoints and diagnostic
observers are caller-owned and retained across restore.

The format is versioned and endian-independent. A checksum detects damaged
state; it is not an authentication mechanism. Restore validates identity,
lengths and hardware layout before changing the destination. Live host clock
time never enters the emulated RTC. `SnapshotRing` allocates its 60 slots once;
capture, lookup and rewind then allocate no memory. Rewind discards newer
history so a corrected replay replaces its original branch.

A complete state is larger than 65 KB: framebuffer, cartridge RAM, FIFO and
audio state also matter. The fixed slot is 256 KiB and its maximum current
serialized payload is about 248 KiB. Sixty slots reserve about 15 MiB. The
snapshot test measures save/restore latency on the actual host and includes
worst-case 128 KiB cartridge RAM and 64 KiB retained serial output. Performance
figures apply to that tested schema and host, not an assumed universal bound.

## Serial rollback over UDP

```sh
python3 tools/verify_netplay.py --frames 1000 --pace-ms 17 --delay-ms 100 \
  --loss 0.05 --output artifacts/my-link-campaign
python3 tools/test_netplay_protocol.py
```

The verifier launches two separate native emulator processes and a real
bidirectional UDP relay. Delay is 100 ms one way with ±20 ms jitter, about
200 ms round-trip before jitter and retransmissions. Five percent of received
datagrams are dropped by the relay. The authored ROM reads the joypad, clocks
SB/SC through real serial transfers, records received bytes in WRAM, and feeds
received bytes into later transmissions. An independent byte recurrence
checks those transfers. Each console's final full snapshot, every confirmed
frame hash, video bytes and PCM samples must match its corresponding clean
run. The two consoles themselves need not have identical states.

The desktop players' interactive friend sessions are documented in [NETPLAY.md](NETPLAY.md).
They use replicated two-console emulation with confirmed inputs and six frames
of input delay; the legacy rollback harness below remains a distinct runner.

`build/netplay` can run on separate hosts with explicit `--bind`,
`--peer`, `--port`, `--peer-port`, `--side`, `--session`, `--rom`, `--peer-rom`,
`--frames`, `--seed`, `--pace-ms`, `--timeout` and `--output` values. The peer
ROM is read only to validate its identity; each process executes one machine.
Both peers must agree on session, ROM identities and frame count. The current
headless runner supplies deterministic seeded input masks; it is an automated
link/replay harness, not an interactive network game frontend.

Packets have explicit endian-independent fields, sequence numbers, a 64-bit
acknowledgment bitmap, session IDs, CRC32, emulated frame/cycle timestamps and
bounded retransmission state. Session IDs and CRC detect stale/corrupt traffic;
they do not authenticate an untrusted peer. Complete versioned event bundles
allow a replay to replace or retract earlier predicted transfers. The serial
shifter still runs one bit per actual clock edge; it is not replaced with a
byte-result shortcut. One console can supply an internal clock while the other
uses the external clock.

Snapshots are taken at real CPU instruction boundaries before each frame
quantum. On a discrepancy, replay restores the latest retained boundary
strictly before the earliest changed serial edge and reexecutes the original
instructions. A frame is publishable only after its exact local/remote
versions, prior-frame versions, and any consumed neighboring-frame prefix
are mutually consistent. A final unpresented lookahead frame resolves CPU
instructions that cross the requested end boundary. Correction handling
never restores a snapshot inside a suspended C++ instruction callback.

The fixed history retains 60 frame snapshots and permits at most 58 frames
of speculation, with bounded event/fragment queues. Exceeding a bound fails
explicitly or applies backpressure; it never silently drops needed history.
The runner currently retains at most 64 KiB of serial transcript through the
snapshot schema. A STOP-gated oscillator produces an explicit diagnostic
instead of an infinite inner loop. The diagnostics and standalone tests cover
malformed frames, inconsistent fragments, sequence wrap, stale acknowledgments,
and closed-window behavior.

Only confirmed video/audio leaves the rollback boundary, avoiding duplicated
or revised samples in exported output. This adds confirmation latency and can
stall presentation under network pressure. The implementation does not claim
sub-millisecond catch-up or seamless interactive play. Reports measure the
whole interval from the first rewind until the old simulation horizon is
reached, including nested corrections. Actual 0.5 ms and frame-rate targets
must be assessed from those measurements.

## Matchaboy Inspector

On Windows use `build\Matchaboy.exe`; on Mac open `build/MatchaAutopsy.app`;
on Linux run `./build/Matchaboy` inside an X11/XWayland desktop.
Without arguments, the players open the original ten-game arcade. Opening a cartridge
shows the large player view, with Inspector available through Tab. F12 captures
the current display; Windows/Mac use the live OpenGL viewport. The window scales the same 1280 by 920 canvas with its
aspect ratio intact. Native CMake builds place the app in the chosen build
directory; the Makefile retains `build/autopsy` as a compatibility entry point.

```sh
./build/autopsy roms/acid2/dmg-acid2.gb --inspector --paused --frames 120 --line 48 --dot 115
./build/autopsy roms/acid2/dmg-acid2.gb --inspector --headless --frames 120 \
  --line 48 --dot 115 --capture /absolute/path/autopsy.png
```

The optional `ENABLE_AUTOPSY` build instruments actual bus transactions and
retired instructions. Release builds compile these hooks out. The 256×256 map
uses low address byte as X and high byte as Y: address `C123` appears at
`(35,193)`. Reads are blue, writes red and instruction fetches green. Counters
are atomic and decay once per emulated frame. The heatmap counts CPU bus
accesses; it excludes DMA and PPU internal fetches. The displayed locks and
FIFOs come from the live machine.

Escape pauses local play. Tab toggles Inspector. Balanced game controls are
WASD, L/K for A/B, Q/I for GBA shoulders, Enter for Start and Space for Select;
Space does not fast-forward. **Keyboard Settings…** (Command-Comma on Mac,
Ctrl-Comma on Windows/Linux) provides editable bindings and Balanced/Classic
presets. Apply persists the layout and updates the visible guide. Bindings are
host preferences, outside deterministic snapshots and network payloads.

Use Command on Mac or Ctrl on Windows/Linux with S/F to step an instruction or
frame. Windows/Mac additionally expose modifier-D for a Game Boy peripheral
dot and modifier-1–4 for Inspector views. Linux currently has a combined
CPU/FIFO/memory inspector. Dot stepping
is deliberately a peripheral-only inspection action, not normal CPU execution;
it is unavailable for GBA and all stepping is disabled while linked. On Windows/Mac, a requested
line/dot capture stops at the next CPU instruction boundary at or beyond the
requested dot; the dashboard reports the actual position. Linux's headless
capture exports actual LCD pixels and metadata; its window capture is a separate
X11 test and should not be described as the Mac/Windows composite renderer.

The four scopes sample real digital APU channel outputs every 32 T-cycles.
They are pulse 1 with sweep, pulse 2, wave RAM and noise LFSR, before stereo
mixing. A silent ROM correctly produces flat scopes. APU state and the integer
high-pass filter participate in deterministic snapshots. Sample synthesis can
be disabled while channel timers and frame-sequencer hardware keep running.

Generate the included executable homebrew to exercise all four channels:

```sh
python3 tools/make_audio_demo.py
./build/autopsy roms/homebrew/silicon_audio.gb --inspector --frames 120
```

Its CPU writes the audio registers and triangle wave RAM. The dashboard shows
the resulting hardware samples. `python3 tools/verify_autopsy.py --output
artifacts/my-hud-test` also captures the real native OpenGL back buffer and
compares it with the headless renderer at the same live FIFO position.

The desktop players output 48 kHz stereo through their operating system when
a device is available. Mac uses Audio Queue callbacks, Windows uses WinMM buffer
completion, and Linux optionally uses nonblocking ALSA output. Library,
pause and mute transitions clear playback; the scopes continue to represent
actual hardware or PCM state. The Windows/Mac GBA Inspector shows display registers,
palette, ARM/Thumb registers, memory pages and final stereo samples. The GB
dot-step and retired-instruction history do not apply to GBA. See
[MACOS.md](MACOS.md) for Mac capture, relocated-bundle and audio checks.

## C and Python learning API

`include/dmg/gym.h` provides the requested C ABI plus checked calls, reset,
terminal predicates, reward watches and direct observation pointers. A step
executes a 70,224-T quantum on every independent machine. Instructions finish
at boundaries and excess clocks carry into the next step. STOP does not
invent oscillator clocks: the caller receives control to supply another
action. Terminal environments stop until explicitly reset.

Each persistent worker has a bounded SPSC task queue. The serialized caller
submits jobs round-robin; only that worker consumes its queue. Push/pop use
lock-free atomic loads/stores and have bounded, wait-free operations. Worker
parking and batch completion can block outside the queues. Each machine has
exactly one owner during a batch. Calls on the same C handle must be serialized.
Python serializes calls with a lock and releases the GIL during native work.
Neither rendering windows, operating-system sleeps nor PCM synthesis runs in
the Gym. The actual PPU still runs and produces correct observation pixels.

```sh
python3 -m venv .venv
.venv/bin/python -m pip install numpy gymnasium
.venv/bin/python -m unittest discover -s tests -p 'test_matcha_gym.py'
```

```python
import numpy as np
from matcha_gym import NativeBatch, MatchaEnv

with NativeBatch("roms/acid2/dmg-acid2.gb", num_instances=16) as batch:
    ram = batch.ram(0)             # borrowed 8192-byte C000-DFFF WRAM
    pixels = batch.observation(0)  # borrowed 144x160 uint8 shades, 0..3
    batch.watch(0xC000, width=2, scale=0.1, delta=True)
    rewards, dones = batch.step(np.zeros(16, dtype=np.uint8))

with MatchaEnv("roms/acid2/dmg-acid2.gb", copy_observations=True) as env:
    observation, info = env.reset(seed=123)
    observation, reward, terminated, truncated, info = env.step(0)
```

Action bits are Right, Left, Up, Down, A, B, Select, Start, with 1 meaning
pressed. RAM watches read little-endian 1/2/4-byte values, optionally signed,
and sum scaled absolute values or deltas into the reward. Game-specific reward
addresses must come from the particular ROM; no commercial game's variables
are guessed. `terminal(address, mask=..., value=...)` sets an episode predicate.

Borrowed RAM and pixel views refer to actual native storage and change on
step/reset. Copy them for replay buffers. Do not read them concurrently with
a native step. Python views retain storage safely even after `close()`; C
pointers expire at `gym_destroy`. `gym_get_observations` and Python
`observations()` explicitly copy; `gym_observation_ptr` and `observation()`
provide the zero-copy alternative. WRAM is a real 8 KiB backing allocation,
not a pretend flat pointer across ROM banks, MMIO and memory aliases. Use
`peek` for other mapped addresses.

## Compare this host with a faster machine

Keep the same project source and original ROM bytes on both hosts. Run a
fresh output directory for each measurement; retain all reports. Avoid other
compiles or benchmarks during the timed sample.

```sh
python3 tools/benchmark_host.py --lto --instances 16 --seconds 10 --warmup 1 \
  --output artifacts/host-baseline

# On the other host, after copying the same project and baseline directory:
python3 tools/benchmark_host.py --lto --instances 16 --seconds 10 --warmup 1 \
  --output artifacts/host-comparison --baseline artifacts/host-baseline/host.json
```

The report retains a source archive and inventory, original ROM SHA-256,
compiler/version/flags, CPU and memory details, worker count, peak process
RSS, actual executed T-cycles, retired instructions, PPU frames, and elapsed
wall time. Aggregate FPS means executed T-cycles divided by 70,224 and by
wall seconds. Paused/terminated calls cannot inflate it. A separate fixed
64-frame input workload produces comparable full-state hashes on both hosts.

The 50,000-FPS target stays in the report. A run below it exits 1 and records
`passed: false`; the measurements remain usable as a baseline. Compiler or
worker-count changes are reported. Different ROMs, sources, optimization
flags, timing windows or accuracy settings invalidate a direct speed ratio.
No claim of cross-platform determinism is made until the new host's hashes
actually match. Run correctness checks there before interpreting performance.

Suggested instruction for the next agent:

> Run the Matcha benchmark and correctness suites on this host using the saved
> baseline's source inventory, ROM hashes, compiler flags, 16 environments,
> warm-up and 10-second measurement window. Compare aggregate FPS, executed
> T-cycles, peak memory, snapshot save/restore latency, rollback latency and
> correctness results. Report hardware/toolchain differences and preserve
> both reports. Keep the workload and accuracy settings unchanged; do not
> lower the acceptance thresholds to make a result pass.

External commercial-ROM validation requires a user-supplied ROM. The authored
Link fixture executes real CPU, timer, PPU and serial hardware, but its results
must not be described as verified Tetris or Pokémon gameplay.
