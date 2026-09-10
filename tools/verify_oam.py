#!/usr/bin/env python3
"""Verify unmodified Blargg OAM-corruption ROMs; preserve every result."""
import argparse
from collections import deque
import hashlib
import json
from pathlib import Path
import platform
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path, data):
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(data, indent=2) + "\n")
    temporary.replace(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--binary", type=Path, default=ROOT / "build" / "dmg")
    parser.add_argument("--only", help="substring filter on original relative ROM name")
    parser.add_argument("--include-overflowing-single", action="store_true",
                        help="also run standalone7, whose original output overruns its executable WRAM; forensic use")
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--max-cycles", type=int, default=4_000_000_000)
    parser.add_argument("--trace-capacity", type=int, default=65_536)
    args = parser.parse_args()
    if min(args.timeout, args.max_cycles, args.trace_capacity) <= 0 or args.trace_capacity > 1_000_000:
        parser.error("bounds must be positive and trace capacity at most 1000000")
    binary = args.binary.resolve()
    output = (args.output or ROOT / "artifacts" / f"oam-{time.time_ns()}").resolve()
    output.mkdir(parents=True, exist_ok=False)
    external = ROOT / "roms" / "blargg"
    manifest = json.loads((ROOT / "roms" / "manifest.json").read_text())
    inventory = {name: item for name, item in manifest["files"].items() if name.startswith("oam_bug/")}
    for name, item in inventory.items():
        if sha(external / name) != item["sha256"]:
            raise RuntimeError(f"original OAM oracle modified: {name}")
    names = sorted(name for name in inventory if name.startswith("oam_bug/rom_singles/") and name.endswith(".gb"))
    if len(names) != 8:
        raise RuntimeError(f"expected all eight original OAM singles, found {len(names)}")
    aggregate = "oam_bug/oam_bug.gb"
    if aggregate not in inventory:
        raise RuntimeError("original aggregate is required to verify all eight OAM modules")
    overflowing = "oam_bug/rom_singles/7-timing_effect.gb"
    excluded = []
    if not args.include_overflowing_single:
        names.remove(overflowing)
        excluded.append({"rom": overflowing,
                         "reason": "original unbounded text log reaches C000 and overwrites its own executable before trial116; module7 is verified by the unchanged aggregate",
                         "evidence": "artifacts/forensics/oam-standalone-overflow.json",
                         "documentation": "OAM_ORACLE.md",
                         "standalone_verdict": "inconclusive; not passed"})
    names.append(aggregate)
    if args.only:
        names = [name for name in names if args.only in name]
    if not names:
        raise RuntimeError("no OAM ROMs selected")
    sources = {path.relative_to(ROOT).as_posix(): sha(path)
               for directory in ("include", "src", "tests", "tools")
               for path in (ROOT / directory).rglob("*")
               if path.is_file() and "__pycache__" not in path.parts}
    sources["Makefile"] = sha(ROOT / "Makefile")
    binary_sha = sha(binary)
    summary = {"passed": False, "platform": platform.platform(), "target": "DMG OAM corruption",
               "repository": manifest["repository"], "revision": manifest["revision"],
               "archive_sha256": manifest["archive_sha256"], "oracle_files": inventory,
               "sources": sources, "binary": str(binary), "binary_sha256": binary_sha,
               "binary_source_relationship": "recorded current source tree; alternate binaries may originate from earlier revisions",
               "selected": names, "excluded": excluded,
               "scope": "seven standalone modules plus aggregate containing all eight modules; standalone7 excluded for documented oracle self-overwrite" if not args.include_overflowing_single else "forensic set including known self-overwriting standalone7",
               "tests": []}
    started = time.monotonic()
    save(output / "summary.json", summary)
    for name in names:
        label = re.sub(r"[^a-zA-Z0-9_-]", "_", name.removesuffix(".gb"))
        report = output / f"{label}.json"
        trace = output / f"{label}.trace.txt"
        stderr = output / f"{label}.stderr.txt"
        stdout = output / f"{label}.stdout.txt"
        command = [str(binary), str(external / name), "--protocol", "blargg",
                   "--timeout", str(args.timeout), "--max-cycles", str(args.max_cycles),
                   "--report", str(report), "--trace-out", str(trace),
                   "--trace-capacity", str(args.trace_capacity),
                   "--frame-out", str(output / f"{label}.pgm")]
        print(f"RUN {name}", flush=True)
        before = time.monotonic()
        failure = None
        with stdout.open("w") as out, stderr.open("w") as err:
            try:
                process = subprocess.run(command, stdout=out, stderr=err,
                                         timeout=args.timeout + 15, check=False)
                exit_code = process.returncode
            except subprocess.TimeoutExpired:
                exit_code = 124
                failure = "external wall-clock timeout; result inconclusive"
            except OSError as error:
                exit_code = 127
                failure = str(error)
        try:
            data = json.loads(report.read_text())
        except (OSError, ValueError):
            data = {"status": "inconclusive_missing_report"}
        if not isinstance(data, dict):
            data = {"status": "inconclusive_invalid_report"}
        data.update({"rom": name, "sha256": inventory[name]["sha256"], "command": command,
                     "exit_code": exit_code, "seconds": time.monotonic() - before})
        if failure:
            data["runner_error"] = failure
        # Neither a frame-count stop nor emulator-generated success text is a
        # verdict. Require the authentic serial/RAM protocol to terminate.
        data["passed"] = (exit_code == 0 and data.get("status") == "passed"
                          and data.get("mechanism") in ("serial", "cartridge RAM signature"))
        if name == aggregate:
            log = stdout.read_text(errors="replace")
            data["all_eight_modules_ok"] = all(re.search(rf"\b{number:02d}:ok\b", log) for number in range(1, 9))
            data["passed"] = data["passed"] and data["all_eight_modules_ok"]
        summary["tests"].append(data)
        print(stdout.read_text(errors="replace").strip(), flush=True)
        print(f"{'PASS' if data['passed'] else 'FAIL'} {name}: {data.get('status')} "
              f"({data.get('t_cycles', '?')} T-cycles)", flush=True)
        if not data["passed"]:
            detail = trace if trace.exists() else stderr
            if detail.exists():
                with detail.open(errors="replace") as stream:
                    print("".join(deque(stream, maxlen=60)), end="", flush=True)
        save(output / "summary.json", summary)
    summary["source_unchanged"] = all((ROOT / name).is_file() and sha(ROOT / name) == value
                                      for name, value in sources.items())
    summary["binary_unchanged"] = sha(binary) == binary_sha
    summary["oracles_unchanged"] = all(sha(external / name) == item["sha256"] for name, item in inventory.items())
    summary["seconds"] = time.monotonic() - started
    summary["passed_count"] = sum(test["passed"] for test in summary["tests"])
    summary["passed"] = (summary["source_unchanged"] and summary["binary_unchanged"]
                         and summary["oracles_unchanged"] and all(test["passed"] for test in summary["tests"]))
    save(output / "summary.json", summary)
    print(f"{'PASS' if summary['passed'] else 'FAIL'} {summary['passed_count']}/{len(names)} original OAM ROMs; {output}", flush=True)
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
