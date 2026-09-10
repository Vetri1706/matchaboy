# Matcha: current-host verification

This is the historical M3 baseline at commit `f25eb24`, before the CMake/Windows
portability additions. Its original reports remain unchanged. For current
downloads and platform coverage, see [DOWNLOADS.md](DOWNLOADS.md); reproduce
the frozen revision before comparing against its exact source inventory.

The frozen platform campaign passes its correctness checks. The requested
50,000 aggregate FPS, unconditional 100-microsecond snapshot bound, and
0.5-millisecond rollback target have **not** been achieved. These are measured
limitations, not acceptance thresholds that were lowered for a passing report.

The host is an Apple M3 with eight CPU cores and 8 GiB RAM, running macOS 26.5.1
and Apple Clang 21. C++ release builds use C++20, `-O3 -Wall -Wextra -Werror
-Wpedantic`; the threaded Gym benchmark also uses `-pthread -flto`.

## Implemented and verified

- Complete, versioned deterministic snapshots, validation before restore,
  zero-allocation capture/restore, and a preallocated 60-frame history.
- Bit-clocked serial hardware, bounded reliable UDP transport, versioned
  transfer corrections, actual CPU rollback/replay, and confirmed audiovisual
  output. Two separate emulator processes communicate through real sockets.
- Optional hardware telemetry and a native macOS OpenGL dashboard displaying
  live FIFO contents, fetch stages, CPU disassembly, memory activity, and four
  actual APU channel waveforms.
- Independent threaded Gym instances, bounded SPSC work queues, C ABI,
  zero-copy NumPy RAM/observation views, reward watches, and a Gymnasium Env.

The full [campaign report](artifacts/platform-verification-2/summary.json)
records `correctness_passed: true`, `host_measurement_valid: true`,
`performance_target_met: false`, and `passed: false`. All frozen source,
executable, and source-archive integrity checks pass. The
[independent audit](artifacts/platform-verification-2/INDEPENDENT_AUDIT.md)
rechecked the original ROM evidence.

| Verification | Result |
| --- | --- |
| Original Mooneye DMG selection | 65/65, including all ten requested cases |
| Original Blargg CPU/timing | 20/20; aggregate modules 01–11 pass |
| Original Blargg OAM | Seven standalone cases and the complete eight-module aggregate pass |
| Original Blargg sound | 12 standalone cases and the complete aggregate pass |
| dmg-acid2 | All 23,040 pixels match at frame 120 |
| Mealybug DMG references | All 552,960 pixels match across 24 cases |
| Core unit/edge assertions | 9,408,187, in optimized and ASan/UBSan runs |
| Platform checks | 36 APU, 298 snapshot, 19 autopsy checks; UDP transport tests pass |
| Gym concurrency | 80,000 SPSC jobs; 16-machine full-state replay; ThreadSanitizer passes |
| Python/Gymnasium | Four tests pass, including the official Env checker |
| Protocol rejection | Seven real UDP malformed-session/STOP cases pass |
| Native GPU dashboard | Real OpenGL readback matches the headless view within interpolation rounding |

This is the documented 131-run ROM selection. Hardware-specific exclusions,
original ROM anomalies, boot/STOP/RTC scope, and reference-image provenance
remain documented in the audit and existing hardware verification guides.
Passing these tests does not establish universal hardware accuracy.

## UDP fault run

The final [1,000-frame campaign](artifacts/platform-verification-2/netplay/summary.json)
uses the original authored Link ROM, with received serial data feeding later
transmissions. Both consoles match their respective clean run byte-for-byte
for final complete state, per-frame state hashes, video, and PCM. An independent
serial-byte recurrence also agrees with WRAM and the peer's transmitted bytes.

The relay applies 100 ms one-way delay, ±20 ms jitter, and 5% configured loss.
It actually drops **26,444 of 528,090 datagrams** and forwards 501,646;
434,161 forwarding events are reordered. Both consoles finish all 1,000 frames.
The clean campaign takes about 17.5 seconds; the fault campaign takes 124.2 seconds.

| Whole catch-up interval | Peer 0 | Peer 1 |
| --- | ---: | ---: |
| Median | 179.143 ms | 199.597 ms |
| 95th percentile | 1,256.38 ms | 1,395.88 ms |
| Maximum | 1,768.37 ms | 3,526.43 ms |
| Rollbacks | 4,929 | 5,122 |
| Replayed frame quanta | 74,660 | 71,224 |
| Maximum rewind | 58 frames | 58 frames |

Catch-up timing includes nested corrections and intervening network/OS work.
It is not the time to copy a snapshot. Confirmed output avoids exporting
revised audio/video but adds latency and can stall. This implementation has
not met the performance bar for seamless competitive play. Faster hardware
alone is not established as a solution to the correction and confirmation costs.

The final [idle-peer run](artifacts/netplay-hotpath-idle/summary.json) also passes:
an unarmed peer supplies idle `0xFF` serial bits. Removing eager instruction
diagnostic formatting cut the clean 1,000-frame time from 146 to 17.5 seconds;
[all twelve full outputs remain identical](artifacts/platform-verification-2/netplay/pre-optimization-equivalence.json).

No commercial ROM was supplied. These results do not certify Tetris or Pokémon
trade/battle compatibility. The current runner uses seeded inputs and exports
confirmed frames/audio; it is not an interactive network game frontend.

## Current-host baseline and repeat

The benchmark executes 16 instances on eight workers for ten seconds after a
one-second warm-up. It resets after warm-up, leaves the real pixel FIFO active,
disables PCM synthesis, and counts actual executed T-cycles divided by 70,224.
The separate fixed 64-frame workload has identical full-state hashes in all
16 environments across the two runs.

| Metric | Baseline | Repeat |
| --- | ---: | ---: |
| Aggregate simulated frames/s | 4,407.63 | 4,337.13 |
| Timed executed T-cycles | 3,096,597,504 | 3,046,036,224 |
| Timed wall seconds | 10.00447 | 10.00108 |
| Peak resident bytes | 7,716,864 | 7,716,864 |
| Save median / p95 | 49.333 / 56.125 µs | 49.875 / 56.000 µs |
| Restore median / p95 | 58.833 / 67.291 µs | 59.667 / 66.334 µs |
| Save / restore maximum | 64.750 / 110.500 µs | 259.875 / 259.583 µs |
| Snapshot allocations | 0 | 0 |

Snapshot samples use 253,408-byte states including worst-case cartridge RAM
and retained serial output, with 500 samples per run. Median latency meets
100 µs; observed maxima do not support an unconditional bound. Sixty complete
snapshot slots reserve about 15 MiB, rather than the proposed 65 KiB per slot.

The [baseline](artifacts/platform-verification-2/host-baseline/host.json) and
[repeat comparison](artifacts/host-comparison-1/host.json) are valid, comparable
measurements; their FPS ratio is 0.9840. Both correctly fail the 50,000-FPS bar.
Each report preserves source, ROM and executable hashes, exact tested binaries,
compiler settings, host metadata, and correctness hashes. A separate
[10-second 16-environment leak run](artifacts/gym-leaks-stress-1/leaks.log)
reports zero leaked bytes. macOS notes its process-inspection security
restriction; this is a tool observation, not a proof of absence of every leak.

## Reproduce on a higher-spec host

Copy the same project source, original ROMs, and baseline artifacts. Keep the
archived inventory unchanged and run from the repository root. Use the optional Python
environment with NumPy/Gymnasium for the complete platform tests.

```sh
python3 tools/verify_platform.py --output artifacts/new-host-platform
python3 tools/benchmark_host.py --lto --instances 16 --seconds 10 --warmup 1 \
  --output artifacts/new-host-comparison \
  --baseline artifacts/platform-verification-2/host-baseline/host.json
```

A below-target run deliberately exits 1 while preserving valid results.
Inspect `correctness_passed`, `measurement_valid`, and `comparison.comparable`
separately. A changed workload or mismatching correctness hashes invalidates
the direct comparison. An actual [one-second workload trial](artifacts/host-comparison-rejection-1/host.json)
correctly rejects comparison against the ten-second baseline and emits no
speed ratio. The headless platform targets POSIX hosts; the native
GUI target currently requires macOS.

Copyable instruction for the next agent:

> Continue Matcha on this host. Read PLATFORM.md and PLATFORM_VERIFICATION.md.
> Use artifacts/platform-verification-2/host-baseline/host.json as the baseline.
> Keep its exact source inventory, ROM hashes, optimization flags, 16 environments,
> one-second warm-up, ten-second measurement window, input seed, and accuracy
> settings. Run the complete correctness suite and the benchmark comparison.
> Compare FPS, executed T-cycles, peak memory, snapshot median/p95/max latency,
> UDP rollback latency, and full-state correctness hashes. Record hardware,
> toolchain and worker-count differences. Preserve both reports, report unmet
> targets explicitly, and do not lower thresholds or substitute an easier ROM.

Frozen source archive SHA-256:
`047f31b99668f0f459a29530bea822054775b3c1a27c63a9a8de42cb0618c927`.

See [PLATFORM.md](PLATFORM.md) for native dashboard commands, C/Python API
contracts, memory views, reward watches, and protocol bounds.
