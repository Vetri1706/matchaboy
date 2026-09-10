#!/usr/bin/env python3
"""Freeze, verify and benchmark the same Matcha platform source on this host."""
import argparse
import json
from pathlib import Path
import platform
import shutil
import signal
import subprocess
import sys
import time

from verify_all import archive_sources, inventory, sha
from benchmark_host import run_owned, interrupt_command

ROOT = Path(__file__).resolve().parents[1]


def main():
    signal.signal(signal.SIGTERM, interrupt_command)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--skip-rom-regression", action="store_true",
                        help="development only: record that the external ROM campaign was omitted")
    args = parser.parse_args()
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=False)
    summary = {"passed": False, "correctness_passed": False, "performance_target_met": False,
               "commercial_rom_verified": False, "sources": inventory(), "commands": [],
               "platform": platform.platform(), "python": sys.version,
               "rom_regression_omitted": args.skip_rom_regression}

    def save():
        temporary = output/"summary.json.tmp"
        temporary.write_text(json.dumps(summary, indent=2)+"\n")
        temporary.replace(output/"summary.json")

    def run(name, command, timeout=900):
        print(f"RUN {name}", flush=True)
        begun = time.monotonic()
        with (output/(name+".log")).open("w") as log:
            code = run_owned(command, log, timeout)
        summary["commands"].append({"name": name, "command": command,
                                    "exit_code": code, "seconds": time.monotonic()-begun})
        save()
        print(f"{'PASS' if code == 0 else 'FAIL'} {name}; {output/(name+'.log')}", flush=True)
        if code == 130: raise KeyboardInterrupt
        return code == 0

    save(); archive_sources(summary["sources"], output/"tested-source.tar.gz")
    summary["source_archive_sha256"] = sha(output/"tested-source.tar.gz")
    if not args.skip_rom_regression:
        run("rom-regression", [sys.executable, "tools/verify_all.py", "--output", str(output/"rom-regression")])
    if not run("build", ["make", "-B", "-j4", "platform", "build/gym_benchmark_lto"]):
        return 2
    names = ["dmg", "snapshot_tests", "apu_tests", "gym_tests", "gym_benchmark_lto", "netplay", "netplay_tests", "autopsy_tests",
             "libmatcha.dylib" if sys.platform == "darwin" else "libmatcha.so"]
    if sys.platform == "darwin": names.append("autopsy")
    summary["binaries"] = {name: sha(ROOT/"build"/name) for name in names}
    (output/"binaries").mkdir()
    for name in names: shutil.copy2(ROOT/"build"/name, output/"binaries"/name)
    save()
    run("native-tests", ["make", "platform-test"])
    run("platform-sanitizers", ["make", "platform-sanitize"])
    run("thread-sanitizer", ["make", "platform-tsan"])
    run("python-gymnasium", [sys.executable, "-m", "unittest", "discover", "-s", "tests", "-p", "test_matcha_gym.py"])
    run("protocol-rejection", [sys.executable, "tools/test_netplay_protocol.py"])
    run("netplay", [sys.executable, "tools/verify_netplay.py", "--frames", "1000", "--output", str(output/"netplay")], 900)
    if sys.platform == "darwin":
        run("native-gpu", [sys.executable, "tools/verify_autopsy.py", "--output", str(output/"autopsy")])
        run("leaks", ["leaks", "--atExit", "--", "build/gym_tests"])
    # Timed samples run last, without our compilers, peers or other test actors.
    run("host-baseline", [sys.executable, "tools/benchmark_host.py", "--lto", "--instances", "16",
                          "--seconds", "10", "--warmup", "1", "--output", str(output/"host-baseline")])
    performance = output/"host-baseline/host.json"
    if performance.exists():
        try:
            summary["performance"] = json.loads(performance.read_text())
            if not isinstance(summary["performance"], dict): raise ValueError("host report must be an object")
        except (OSError, ValueError) as error:
            summary["performance"] = {}; summary["performance_error"] = str(error)
        summary["performance_target_met"] = summary["performance"].get("performance_target_met") is True
    host_command = next(command for command in summary["commands"] if command["name"] == "host-baseline")
    summary["host_measurement_valid"] = (host_command["exit_code"] in (0, 1)
        and summary.get("performance", {}).get("measurement_valid") is True)
    summary["sources_unchanged"] = inventory() == summary["sources"]
    summary["binaries_unchanged"] = all(sha(ROOT/"build"/name) == value and sha(output/"binaries"/name) == value
                                         for name, value in summary["binaries"].items())
    summary["source_archive_unchanged"] = sha(output/"tested-source.tar.gz") == summary["source_archive_sha256"]
    summary["correctness_passed"] = (summary["sources_unchanged"] and summary["binaries_unchanged"]
        and summary["source_archive_unchanged"] and not args.skip_rom_regression and summary["host_measurement_valid"]
        and all(c["exit_code"] == 0 for c in summary["commands"] if c["name"] != "host-baseline"))
    summary["passed"] = summary["correctness_passed"] and summary["performance_target_met"]
    summary["scope"] = "Original DMG acceptance ROMs, authored real-CPU link fixture, current-host measurements; no commercial-ROM claim."
    save()
    print(json.dumps({key: summary[key] for key in ("passed", "correctness_passed", "performance_target_met",
                                                  "commercial_rom_verified")}, indent=2))
    print(output/"summary.json")
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    try: sys.exit(main())
    except KeyboardInterrupt: sys.exit(130)
    except OSError as error:
        print(f"platform verification failed: {error}", file=sys.stderr); sys.exit(2)
