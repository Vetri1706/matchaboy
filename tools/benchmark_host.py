#!/usr/bin/env python3
"""Record a reproducible host benchmark and compare only matching workloads."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import shutil
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from verify_all import inventory, archive_sources


def run_owned(command, log, timeout, cwd=ROOT):
    """Own the process group, including make/compiler and verifier descendants."""
    process = None
    try:
        process = subprocess.Popen(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        return process.wait(timeout=timeout)
    except OSError as error:
        log.write(f"Could not execute command: {error}\n"); log.flush()
        return 127
    except (subprocess.TimeoutExpired, KeyboardInterrupt) as error:
        if process is not None:
            # SIGTERM gives nested Python wrappers time to clean their own groups.
            try: os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError: pass
            try: process.wait(timeout=2)
            except subprocess.TimeoutExpired: pass
            try: os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError: pass
            process.wait()
        return 130 if isinstance(error, KeyboardInterrupt) else 124


def interrupt_command(_signal, _frame):
    raise KeyboardInterrupt


def finite(value, minimum=0):
    return type(value) in (int, float) and math.isfinite(value) and value >= minimum


def snapshot_valid(data):
    if not isinstance(data, dict) or type(data.get("bytes")) is not int or data["bytes"] <= 0:
        return False
    if data.get("samples") != 500 or data.get("allocations") != 0:
        return False
    for operation in ("save", "restore"):
        values = [data.get(operation + suffix) for suffix in ("_median_us", "_p95_us", "_max_us")]
        if not all(finite(value) for value in values) or not values[0] <= values[1] <= values[2]:
            return False
    return True


def benchmark_valid(data, exit_code):
    if not isinstance(data, dict): return False
    integer_keys = ("instances", "workers", "executed_t_cycles", "instructions", "completed_ppu_frames", "batch_steps")
    if any(type(data.get(key)) is not int or data[key] < 0 for key in integer_keys): return False
    if not 1 <= data["workers"] <= data["instances"] <= 256 or data["batch_steps"] == 0: return False
    if not all(finite(data.get(key)) for key in ("warmup_seconds", "requested_seconds", "seconds", "minimum_fps", "aggregate_fps", "cycle_equivalent_frames")): return False
    if data["seconds"] <= 0 or data["requested_seconds"] <= 0 or data["executed_t_cycles"] == 0: return False
    if data["seconds"] + 1e-9 < data["requested_seconds"]: return False
    if data.get("frame_quantum_t_cycles") != 70224 or data.get("input_seed") != 1296127043: return False
    if data.get("audio_synthesis") is not False or data.get("ppu_enabled") is not True or data.get("accuracy_shortcuts") is not False: return False
    frames = data["executed_t_cycles"] / 70224
    if not math.isclose(data["cycle_equivalent_frames"], frames, rel_tol=1e-9, abs_tol=1e-7): return False
    if not math.isclose(data["aggregate_fps"], frames / data["seconds"], rel_tol=1e-9, abs_tol=1e-7): return False
    hashes = data.get("correctness_hashes")
    if data.get("correctness_frames_per_instance") != 64 or not isinstance(hashes, list) or len(hashes) != data["instances"]: return False
    if any(type(value) is not int or not 0 <= value < 2**64 for value in hashes): return False
    target_met = data["aggregate_fps"] >= data["minimum_fps"]
    return data.get("passed") is target_met and exit_code == (0 if target_met else 1)


def unchanged(path, expected):
    try: return sha(path) == expected
    except OSError: return False


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def command_output(command):
    try:
        result = subprocess.run(command, capture_output=True, text=True, check=False, timeout=15)
    except (OSError, subprocess.TimeoutExpired):
        return None
    return result.stdout.strip() if result.returncode == 0 else None


def hardware():
    if sys.platform == "darwin":
        return {"cpu_model": command_output(["sysctl", "-n", "machdep.cpu.brand_string"]),
                "physical_cpus": command_output(["sysctl", "-n", "hw.physicalcpu"]),
                "memory_bytes": command_output(["sysctl", "-n", "hw.memsize"])}
    result = {"cpu_model": platform.processor() or None, "physical_cpus": None, "memory_bytes": None}
    try:
        blocks = Path("/proc/cpuinfo").read_text().strip().split("\n\n")
        records = [dict(line.split(":", 1) for line in block.splitlines() if ":" in line) for block in blocks]
        records = [{key.strip(): value.strip() for key, value in record.items()} for record in records]
        if records:
            result["cpu_model"] = records[0].get("model name", records[0].get("Hardware", result["cpu_model"]))
            cores = {(record["physical id"], record["core id"]) for record in records
                     if "physical id" in record and "core id" in record}
            result["physical_cpus"] = len(cores) or None
        for line in Path("/proc/meminfo").read_text().splitlines():
            if line.startswith("MemTotal:"):
                result["memory_bytes"] = int(line.split()[1]) * 1024
    except OSError:
        pass
    if hasattr(os, "sched_getaffinity"):
        result["available_cpu_affinity"] = len(os.sched_getaffinity(0))
    return result


def main():
    signal.signal(signal.SIGTERM, interrupt_command)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, default=ROOT / "roms/acid2/dmg-acid2.gb")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--instances", type=int, default=16)
    parser.add_argument("--workers", type=int, default=0)
    parser.add_argument("--seconds", type=float, default=10)
    parser.add_argument("--warmup", type=float, default=1)
    parser.add_argument("--min-fps", type=float, default=50000)
    parser.add_argument("--lto", action="store_true")
    args = parser.parse_args()
    if not 1 <= args.instances <= 256 or args.workers < 0 or not finite(args.seconds) or args.seconds == 0 or not finite(args.warmup) or not finite(args.min_fps):
        parser.error("invalid instance, worker, duration or threshold value")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    report = {"passed": False, "measurement_valid": False, "performance_target_met": False,
        "sources": inventory(), "host": {
        "platform": platform.platform(), "machine": platform.machine(),
        "load_average_before": list(os.getloadavg()) if hasattr(os, "getloadavg") else None,
        "logical_cpus": os.cpu_count(), "python": sys.version,
        **hardware(),
    }, "compiler": command_output(["clang++", "--version"]),
        "flags": "-std=c++20 -O3 -Wall -Wextra -Werror -Wpedantic -pthread" + (" -flto" if args.lto else ""),
        "rom_sha256": sha(args.rom.resolve()), "started_unix": time.time()}
    destination = output / "host.json"
    def save():
        temporary = destination.with_suffix(".json.tmp")
        temporary.write_text(json.dumps(report, indent=2) + "\n")
        temporary.replace(destination)
    save()
    archive_sources(report["sources"], output / "tested-source.tar.gz")
    report["source_archive_sha256"] = sha(output / "tested-source.tar.gz")
    target = "build/gym_benchmark_lto" if args.lto else "build/gym_benchmark"
    build = ["make", "-B", target, "build/snapshot_tests"]
    with (output / "build.log").open("w") as log:
        build_code = run_owned(build, log, 300)
    report["build"] = {"command": build, "exit_code": build_code}
    save()
    if build_code:
        return 130 if build_code == 130 else 2
    binary = ROOT / target
    report["binary_sha256"] = sha(binary)
    report["snapshot_binary_sha256"] = sha(ROOT / "build/snapshot_tests")
    (output / "binaries").mkdir()
    preserved_binary = output / "binaries" / binary.name
    preserved_snapshot = output / "binaries/snapshot_tests"
    shutil.copy2(binary, preserved_binary)
    shutil.copy2(ROOT / "build/snapshot_tests", preserved_snapshot)
    report["snapshot_flags"] = "-std=c++20 -O3 -Wall -Wextra -Werror -Wpedantic"
    command = [str(binary), str(args.rom.resolve()), "--instances", str(args.instances),
               "--workers", str(args.workers), "--seconds", str(args.seconds),
               "--warmup", str(args.warmup), "--min-fps", str(args.min_fps),
               "--report", str(output / "benchmark.json")]
    with (output / "benchmark.log").open("w") as log:
        exit_code = run_owned(command, log, max(60, args.seconds + args.warmup + 30))
    report["command"] = command
    report["exit_code"] = exit_code
    if exit_code == 130:
        save(); return 130
    report["host"]["load_average_after"] = list(os.getloadavg()) if hasattr(os, "getloadavg") else None
    try:
        benchmark_data = json.loads((output / "benchmark.json").read_text())
        if not isinstance(benchmark_data, dict): raise ValueError("benchmark report must be an object")
        report["benchmark"] = benchmark_data
    except (OSError, ValueError) as error:
        report["benchmark_error"] = str(error)
    with (output / "snapshot.log").open("w") as log:
        report["snapshot_exit_code"] = run_owned([str(ROOT / "build/snapshot_tests")], log, 60)
    try:
        records = [json.loads(line.removeprefix("SNAPSHOT_BENCHMARK "))
                   for line in (output / "snapshot.log").read_text().splitlines()
                   if line.startswith("SNAPSHOT_BENCHMARK ")]
        if len(records) != 1: raise ValueError("expected exactly one snapshot benchmark record")
        report["snapshot"] = records[0]
    except (OSError, ValueError) as error:
        report["snapshot_error"] = str(error)
    report["snapshot_binary_unchanged"] = unchanged(ROOT / "build/snapshot_tests", report["snapshot_binary_sha256"])
    report["sources_unchanged"] = inventory() == report["sources"]
    report["binary_unchanged"] = unchanged(binary, report["binary_sha256"])
    report["rom_unchanged"] = unchanged(args.rom.resolve(), report["rom_sha256"])
    report["source_archive_unchanged"] = unchanged(output / "tested-source.tar.gz", report["source_archive_sha256"])
    report["preserved_binaries_unchanged"] = (unchanged(preserved_binary, report["binary_sha256"])
        and unchanged(preserved_snapshot, report["snapshot_binary_sha256"]))
    benchmark = report.get("benchmark", {})
    report["benchmark_result_valid"] = benchmark_valid(benchmark, exit_code) and all(
        benchmark.get(key) == expected for key, expected in {
            "rom_sha256": report["rom_sha256"], "instances": args.instances,
            "requested_seconds": args.seconds, "warmup_seconds": args.warmup,
            "minimum_fps": args.min_fps}.items())
    report["snapshot_result_valid"] = report["snapshot_exit_code"] == 0 and snapshot_valid(report.get("snapshot"))
    report["measurement_valid"] = (report["benchmark_result_valid"] and report["sources_unchanged"]
                         and report["binary_unchanged"] and report["rom_unchanged"] and report["source_archive_unchanged"]
                         and report["snapshot_result_valid"] and report["snapshot_binary_unchanged"] and report["preserved_binaries_unchanged"])
    report["performance_target_met"] = report["benchmark_result_valid"] and benchmark.get("passed") is True
    report["passed"] = report["measurement_valid"] and report["performance_target_met"]
    if args.baseline:
        try: baseline = json.loads(args.baseline.read_text())
        except (OSError, ValueError) as error: baseline = {}; report["baseline_error"] = str(error)
        if not isinstance(baseline, dict): baseline = {}
        keys = ("rom_sha256", "instances", "warmup_seconds", "requested_seconds", "minimum_fps",
                "frame_quantum_t_cycles", "input_seed", "audio_synthesis", "ppu_enabled", "accuracy_shortcuts",
                "correctness_frames_per_instance")
        a, b = baseline.get("benchmark", {}), report.get("benchmark", {})
        if not isinstance(a, dict): a = {}
        mismatch = [key for key in keys if a.get(key) != b.get(key)]
        if baseline.get("sources") != report["sources"]: mismatch.append("source_inventory")
        if baseline.get("flags") != report["flags"]: mismatch.append("compiler_flags")
        if baseline.get("snapshot_flags") != report["snapshot_flags"]: mismatch.append("snapshot_compiler_flags")
        for gate in ("measurement_valid", "sources_unchanged", "binary_unchanged", "rom_unchanged", "source_archive_unchanged", "snapshot_binary_unchanged", "preserved_binaries_unchanged"):
            if baseline.get(gate) is not True: mismatch.append("baseline_" + gate)
        if baseline.get("snapshot_exit_code") != 0 or not snapshot_valid(baseline.get("snapshot")):
            mismatch.append("baseline_snapshot_result")
        if not benchmark_valid(a, baseline.get("exit_code")): mismatch.append("baseline_benchmark_result")
        if not report["measurement_valid"]: mismatch.append("current_measurement_invalid")
        if not a.get("correctness_hashes") or a.get("correctness_hashes") != b.get("correctness_hashes"):
            mismatch.append("correctness_hashes")
        report["comparison"] = {"baseline": str(args.baseline.resolve()), "comparable": not mismatch,
            "mismatches": mismatch, "toolchain_changed": baseline.get("compiler") != report["compiler"],
            "worker_count_changed": a.get("workers") != b.get("workers"),
            "correctness_hashes_match": bool(a.get("correctness_hashes")) and a.get("correctness_hashes") == b.get("correctness_hashes")}
        if not mismatch and a.get("aggregate_fps", 0) > 0:
            report["comparison"]["fps_ratio"] = b["aggregate_fps"] / a["aggregate_fps"]
            old_snapshot, new_snapshot = baseline.get("snapshot", {}), report.get("snapshot", {})
            if old_snapshot.get("bytes") == new_snapshot.get("bytes") and old_snapshot.get("samples") == new_snapshot.get("samples"):
                report["comparison"]["snapshot_latency_ratio"] = {
                    key: new_snapshot[key] / old_snapshot[key]
                    for key in ("save_median_us", "save_p95_us", "restore_median_us", "restore_p95_us")
                    if old_snapshot.get(key, 0) > 0 and key in new_snapshot}
        if mismatch or not report["comparison"]["correctness_hashes_match"]:
            report["passed"] = False
    save()
    fps = report.get("benchmark", {}).get("aggregate_fps")
    print(json.dumps({"passed": report["passed"], "measurement_valid": report["measurement_valid"],
                      "performance_target_met": report["performance_target_met"], "aggregate_fps": fps,
                      "report": str(destination), "comparison": report.get("comparison")}, indent=2))
    return 0 if report["passed"] else 1 if report["measurement_valid"] else 2


if __name__ == "__main__":
    try: sys.exit(main())
    except KeyboardInterrupt: sys.exit(130)
    except OSError as error:
        print(f"benchmark capture failed: {error}", file=sys.stderr); sys.exit(2)
