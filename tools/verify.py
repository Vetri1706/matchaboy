#!/usr/bin/env python3
"""Run immutable, external ROM tests and retain every attempt with source hashes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
CPU_SINGLES = (
    "01-special.gb", "02-interrupts.gb", "03-op sp,hl.gb", "04-op r,imm.gb",
    "05-op rp.gb", "06-ld r,r.gb", "07-jr,jp,call,ret,rst.gb", "08-misc instrs.gb",
    "09-op r,r.gb", "10-bit ops.gb", "11-op a,(hl).gb",
)
MEMORY_SINGLES = ("01-read_timing.gb", "02-write_timing.gb", "03-modify_timing.gb")
BASE_ROMS = tuple("cpu_instrs/individual/" + name for name in CPU_SINGLES) + (
    "cpu_instrs/cpu_instrs.gb", "instr_timing/instr_timing.gb",
)
EXTRA_ROMS = tuple(prefix + name for prefix in ("mem_timing/individual/", "mem_timing-2/rom_singles/")
                   for name in MEMORY_SINGLES) + ("halt_bug.gb",)
SOUND_SINGLES = (
    "01-registers.gb", "02-len ctr.gb", "03-trigger.gb", "04-sweep.gb",
    "05-sweep details.gb", "06-overflow on trigger.gb", "07-len sweep period sync.gb",
    "08-len ctr during power.gb", "09-wave read while on.gb", "10-wave trigger while on.gb",
    "11-regs after power.gb", "12-wave write while on.gb",
)
SOUND_ROMS = tuple("dmg_sound/rom_singles/" + name for name in SOUND_SINGLES) + ("dmg_sound/dmg_sound.gb",)


def save(path, data):
    temporary = path.with_name(path.name + ".tmp")
    with temporary.open("w") as stream:
        json.dump(data, stream, indent=2)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, default=ROOT / "build/dmg")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--extra", action="store_true", help="also run original memory timing and HALT bug ROMs")
    parser.add_argument("--sound", action="store_true", help="run the 12 original DMG sound tests and their aggregate instead of CPU tests")
    parser.add_argument("--only", help="substring filter on relative ROM path")
    parser.add_argument("--build-check", action="store_true", help="compile, unit-test and run ASan/UBSan before ROMs")
    args = parser.parse_args()
    binary = args.binary.resolve()
    if args.build_check and binary != (ROOT / "build/dmg").resolve():
        parser.error("--build-check only builds the default build/dmg executable")
    output = args.output or ROOT / "artifacts" / f"run-{time.time_ns()}"
    output.mkdir(parents=True, exist_ok=False)
    checks = []
    if args.build_check:
        for label,command in (("build",["make","-j2"]),("unit",["make","test"]),("sanitizer",["make","sanitize"])):
            start=time.monotonic()
            with (output/f"{label}.txt").open("w") as log:
                result=subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=False)
            checks.append({"name":label,"command":command,"exit_code":result.returncode,"seconds":time.monotonic()-start})
            print((output/f"{label}.txt").read_text(),flush=True)
            save(output / "build-checks.json", checks)
            if result.returncode!=0:
                return 1
    manifest = json.loads((ROOT / "roms" / "manifest.json").read_text())
    rom_root = ROOT / "roms" / "blargg"
    # Explicit names prevent missing individual ROMs from silently shrinking a run.
    if args.sound and args.extra:
        parser.error("--sound and --extra select different suites")
    names = list(SOUND_ROMS if args.sound else BASE_ROMS + (EXTRA_ROMS if args.extra else ()))
    for name in names:
        if name not in manifest["files"] or not (rom_root / name).is_file():
            raise RuntimeError(f"required original ROM missing: {name}")
    if len(names) != (20 if args.extra else 13):
        raise RuntimeError("incomplete built-in Blargg selection")
    for name, item in manifest["files"].items():
        if sha(rom_root / name) != item["sha256"]:
            raise RuntimeError(f"external oracle integrity mismatch: {name}")
    manifest_sha = sha(ROOT / "roms" / "manifest.json")
    if args.only:
        names = [name for name in names if args.only in name]
    if not names:
        raise RuntimeError("no test ROMs selected; run tools/fetch_roms.py first")
    paths = [rom_root / name for name in names]
    sources = {str(p.relative_to(ROOT)): sha(p) for directory in ("include", "src", "tests", "tools")
               for p in (ROOT / directory).rglob("*") if p.is_file() and "__pycache__" not in p.parts}
    sources["Makefile"] = sha(ROOT / "Makefile")
    binary_sha = sha(binary)
    summary = {"passed": False, "platform": platform.platform(), "rom_revision": manifest["revision"],
               "sources": sources, "binary": str(binary), "binary_sha256": binary_sha,
               "build_checks": checks, "selected": names,
               "oracle_files": manifest["files"], "oracle_manifest_sha256": manifest_sha, "tests": []}
    started = time.monotonic()
    save(output / "summary.json", summary)
    for path in paths:
        relative = str(path.relative_to(rom_root))
        expected = manifest["files"][relative]["sha256"]
        if sha(path) != expected:
            raise RuntimeError(f"external ROM integrity mismatch: {relative}")
        label = re.sub(r"[^a-zA-Z0-9_-]", "_", relative.removesuffix(".gb"))
        report = output / f"{label}.json"
        command = [str(binary), str(path), "--report", str(report),
                   "--trace-out", str(output / f"{label}.trace.txt"),
                   "--frame-out", str(output / f"{label}.pgm"), "--timeout", "180"]
        # The original HALT test declares MBC1+RAM (02) but zero RAM size.
        # It writes the verdict to a development cartridge's A000 RAM. Model
        # that physical cartridge explicitly; do not alter the binary/header.
        if relative == "halt_bug.gb":
            command += ["--ram-size", "8192"]
        print(f"RUN {relative}", flush=True)
        with (output / f"{label}.stdout.txt").open("w") as stdout, (output / f"{label}.stderr.txt").open("w") as stderr:
            try:
                result = subprocess.run(command, stdout=stdout, stderr=stderr, timeout=195, check=False)
                exit_code = result.returncode
            except subprocess.TimeoutExpired:
                exit_code = 124
        data = json.loads(report.read_text()) if report.exists() else {"status": "runner_error"}
        data.update({"rom": relative, "sha256": expected, "exit_code": exit_code})
        serial = (output / f"{label}.stdout.txt").read_text(errors="replace")
        if relative == "cpu_instrs/cpu_instrs.gb":
            data["all_eleven_modules_ok"] = all(re.search(rf"\b{n:02d}:ok\b", serial) for n in range(1,12))
            if not data["all_eleven_modules_ok"]:
                data["status"] = "failed_module_output"
        if relative == "dmg_sound/dmg_sound.gb":
            data["all_twelve_modules_ok"] = all(re.search(rf"\b{n:02d}:ok\b", serial) for n in range(1,13))
            if not data["all_twelve_modules_ok"]:
                data["status"] = "failed_module_output"
        summary["tests"].append(data)
        print(serial.strip(), flush=True)
        print(f"RESULT {data['status']} ({data.get('t_cycles', '?')} T-cycles)", flush=True)
        save(output / "summary.json", summary)
    summary["source_unchanged"] = all(sha(ROOT / path) == value for path,value in sources.items())
    summary["binary_unchanged"] = binary.is_file() and sha(binary) == binary_sha
    summary["oracles_unchanged"] = (
        sha(ROOT / "roms" / "manifest.json") == manifest_sha
        and all((rom_root / name).is_file() and sha(rom_root / name) == item["sha256"]
                for name, item in manifest["files"].items()))
    summary["seconds"] = time.monotonic() - started
    summary["passed"] = summary["source_unchanged"] and summary["binary_unchanged"] and summary["oracles_unchanged"] and all(t["status"] == "passed" and t["exit_code"] == 0 for t in summary["tests"])
    save(output / "summary.json", summary)
    print(f"{'PASS' if summary['passed'] else 'FAIL'} {len(paths)} ROMs; evidence: {output}", flush=True)
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
