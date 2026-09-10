#!/usr/bin/env python3
"""Compare original Mealybug capture frames with the author's unmodified DMG PNGs."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

from verify_all import inventory
from verify_ppu import png_gray, write_png

ROOT = Path(__file__).resolve().parents[1]
WINDOW_CASES = {
    "m2_win_en_toggle", "m3_lcdc_win_en_change_multiple", "m3_lcdc_win_en_change_multiple_wx",
    "m3_window_timing", "m3_window_timing_wx_0", "m3_wx_4_change",
    "m3_wx_4_change_sprites", "m3_wx_5_change", "m3_wx_6_change",
}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path, data):
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(data, indent=2) + "\n")
    temporary.replace(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/dmg")
    parser.add_argument("--only", help="substring filter for diagnosis")
    parser.add_argument("--windows", action="store_true", help="run window cases only")
    args = parser.parse_args()
    output = (args.output or ROOT / "artifacts" / f"mealybug-{time.time_ns()}").resolve()
    output.mkdir(parents=True, exist_ok=False)
    external = ROOT / "roms/mealybug"
    original = ROOT / "roms/mealybug-source"
    manifests = {base: json.loads((base / "manifest.json").read_text()) for base in (external, original)}
    manifest_hashes = {base: sha(base / "manifest.json") for base in manifests}
    for base, manifest in manifests.items():
        for name, item in manifest["files"].items():
            if sha(base / name) != item["sha256"]:
                raise RuntimeError(f"original oracle modified: {base / name}")
    prefix = "expected/DMG-blob/"
    expected_names = sorted(Path(name).stem for name in manifests[original]["files"]
                            if name.startswith(prefix) and name.endswith(".png"))
    if len(expected_names) != 24 or not WINDOW_CASES <= set(expected_names):
        raise RuntimeError("incomplete pinned DMG reference selection")
    names = expected_names
    if args.windows:
        names = [name for name in names if name in WINDOW_CASES]
    if args.only:
        names = [name for name in names if args.only in name]
    if not names:
        raise RuntimeError("no Mealybug ROMs selected")
    for name in names:
        if name + ".gb" not in manifests[external]["files"]:
            raise RuntimeError(f"missing original ROM: {name}")
    binary = args.binary.resolve()
    binary_hash = sha(binary)
    sources = inventory()
    summary = {"passed": False, "binary": str(binary), "binary_sha256": binary_hash,
               "sources": sources, "selected": names, "tests": [],
               "original_binary_manifest": manifests[external], "original_source_manifest": manifests[original],
               "oracle_provenance": manifests[original]["oracle_provenance"],
               "excluded": [{"rom": name, "reason": "no author-published DMG reference PNG; CGB-only reference"}
                            for name in sorted(manifests[external]["files"])
                            if name.endswith(".gb") and Path(name).stem not in expected_names]}
    save(output / "summary.json", summary)
    for name in names:
        directory = output / name
        directory.mkdir()
        expected = png_gray((original / prefix / (name + ".png")).read_bytes())
        command = [str(binary), str(external / (name + ".gb")), "--protocol", "mealybug",
                   "--timeout", "30", "--max-cycles", "50000000",
                   "--report", str(directory / "run.json"), "--frame-out", str(directory / "actual.pgm"),
                   "--trace-out", str(directory / "trace.txt"), "--trace-capacity", "65536"]
        with (directory / "stdout.txt").open("w") as stdout, (directory / "stderr.txt").open("w") as stderr:
            try:
                result = subprocess.run(command, stdout=stdout, stderr=stderr, timeout=40, check=False)
                exit_code = result.returncode
            except subprocess.TimeoutExpired:
                exit_code = 124
        try:
            report = json.loads((directory / "run.json").read_text())
            data = (directory / "actual.pgm").read_bytes()
            if not data.startswith(b"P5\n160 144\n255\n"):
                raise ValueError("invalid framebuffer header")
            actual = data.split(b"\n", 3)[3]
            if len(actual) != 23040 or len(expected) != 23040:
                raise ValueError("invalid framebuffer dimensions")
            mismatch = [{"x": i % 160, "y": i // 160, "actual": a, "expected": e}
                        for i, (a, e) in enumerate(zip(actual, expected)) if a != e]
            passed = exit_code == 0 and report.get("status") == "captured" and not mismatch
            test = {"name": name, "passed": passed, "exit_code": exit_code, "run": report,
                    "mismatching_pixels": len(mismatch), "first_mismatches": mismatch[:100], "command": command}
            write_png(directory / "actual.png", actual)
            write_png(directory / "expected.png", expected)
        except (OSError, ValueError) as error:
            test = {"name": name, "passed": False, "exit_code": exit_code, "error": str(error)}
        summary["tests"].append(test)
        save(output / "summary.json", summary)
        print(f"{'PASS' if test['passed'] else 'FAIL'} {name}: {test.get('mismatching_pixels', 'unknown')} differing pixels", flush=True)
    summary["sources_unchanged"] = inventory() == sources
    summary["binary_unchanged"] = sha(binary) == binary_hash
    summary["oracles_unchanged"] = all(sha(base / "manifest.json") == manifest_hashes[base]
        and all(sha(base / name) == item["sha256"] for name, item in manifest["files"].items())
        for base, manifest in manifests.items())
    summary["passed"] = (summary["sources_unchanged"] and summary["binary_unchanged"] and summary["oracles_unchanged"]
                         and all(test["passed"] for test in summary["tests"]))
    summary["passed_count"] = sum(test["passed"] for test in summary["tests"])
    save(output / "summary.json", summary)
    print(f"{'PASS' if summary['passed'] else 'FAIL'} {summary['passed_count']}/{len(names)} original DMG Mealybug images; {output}")
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
