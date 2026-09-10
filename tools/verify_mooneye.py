#!/usr/bin/env python3
"""Run unmodified official Mooneye acceptance ROMs with retained evidence."""
import argparse
from collections import deque
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
REQUESTED = [
    "acceptance/timer/div_write.gb", "acceptance/timer/tim00.gb",
    "acceptance/timer/tim01.gb", "acceptance/timer/tim10.gb",
    "acceptance/timer/tim11.gb", "acceptance/timer/tima_reload.gb",
    "acceptance/ppu/stat_irq_blocking.gb", "acceptance/ppu/intr_2_0_timing.gb",
    "acceptance/oam_dma_restart.gb", "acceptance/bits/mem_oam.gb",
]


# DMG A/B/C selection: 62 runtime tests plus three verified boot-state oracles.
DMG_BOOT = (
    'acceptance/boot_div-dmgABCmgb.gb',
    'acceptance/boot_hwio-dmgABCmgb.gb',
    'acceptance/boot_regs-dmgABC.gb',
)
DMG_ACCEPTANCE = (
    'acceptance/add_sp_e_timing.gb',
    'acceptance/bits/mem_oam.gb',
    'acceptance/bits/reg_f.gb',
    'acceptance/bits/unused_hwio-GS.gb',
    *DMG_BOOT,
    'acceptance/call_cc_timing.gb',
    'acceptance/call_cc_timing2.gb',
    'acceptance/call_timing.gb',
    'acceptance/call_timing2.gb',
    'acceptance/di_timing-GS.gb',
    'acceptance/div_timing.gb',
    'acceptance/ei_sequence.gb',
    'acceptance/ei_timing.gb',
    'acceptance/halt_ime0_ei.gb',
    'acceptance/halt_ime0_nointr_timing.gb',
    'acceptance/halt_ime1_timing.gb',
    'acceptance/halt_ime1_timing2-GS.gb',
    'acceptance/if_ie_registers.gb',
    'acceptance/instr/daa.gb',
    'acceptance/interrupts/ie_push.gb',
    'acceptance/intr_timing.gb',
    'acceptance/jp_cc_timing.gb',
    'acceptance/jp_timing.gb',
    'acceptance/ld_hl_sp_e_timing.gb',
    'acceptance/oam_dma/basic.gb',
    'acceptance/oam_dma/reg_read.gb',
    'acceptance/oam_dma/sources-GS.gb',
    'acceptance/oam_dma_restart.gb',
    'acceptance/oam_dma_start.gb',
    'acceptance/oam_dma_timing.gb',
    'acceptance/pop_timing.gb',
    'acceptance/ppu/hblank_ly_scx_timing-GS.gb',
    'acceptance/ppu/intr_1_2_timing-GS.gb',
    'acceptance/ppu/intr_2_0_timing.gb',
    'acceptance/ppu/intr_2_mode0_timing.gb',
    'acceptance/ppu/intr_2_mode0_timing_sprites.gb',
    'acceptance/ppu/intr_2_mode3_timing.gb',
    'acceptance/ppu/intr_2_oam_ok_timing.gb',
    'acceptance/ppu/lcdon_timing-GS.gb',
    'acceptance/ppu/lcdon_write_timing-GS.gb',
    'acceptance/ppu/stat_irq_blocking.gb',
    'acceptance/ppu/stat_lyc_onoff.gb',
    'acceptance/ppu/vblank_stat_intr-GS.gb',
    'acceptance/push_timing.gb',
    'acceptance/rapid_di_ei.gb',
    'acceptance/ret_cc_timing.gb',
    'acceptance/ret_timing.gb',
    'acceptance/reti_intr_timing.gb',
    'acceptance/reti_timing.gb',
    'acceptance/rst_timing.gb',
    'acceptance/timer/div_write.gb',
    'acceptance/timer/rapid_toggle.gb',
    'acceptance/timer/tim00.gb',
    'acceptance/timer/tim00_div_trigger.gb',
    'acceptance/timer/tim01.gb',
    'acceptance/timer/tim01_div_trigger.gb',
    'acceptance/timer/tim10.gb',
    'acceptance/timer/tim10_div_trigger.gb',
    'acceptance/timer/tim11.gb',
    'acceptance/timer/tim11_div_trigger.gb',
    'acceptance/timer/tima_reload.gb',
    'acceptance/timer/tima_write_reloading.gb',
    'acceptance/timer/tma_write_reloading.gb',
)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def dump_json(path, data):
    temporary = path.with_name(path.name + ".tmp")
    with temporary.open("w") as stream:
        json.dump(data, stream, indent=2)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)


def exclusion(name):
    stem = Path(name).stem
    if "-" in stem:
        suffix = stem.rsplit("-", 1)[1]
        if suffix not in ("G", "GS") and "dmgABC" not in suffix:
            return f"hardware suffix {suffix} does not target DMG CPU A/B/C"
    if stem.startswith("boot_") and name not in DMG_BOOT:
        return "serial clock boot-phase alignment remains outside the verified startup model"
    return None


def tail(path):
    if not path.exists():
        return
    with path.open(errors="replace") as stream:
        print("".join(deque(stream, maxlen=60)), end="", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/dmg",
                        help="emulator executable (allows isolated diagnostic builds)")
    parser.add_argument("--all-acceptance", action="store_true",
                        help="run 62 DMG runtime acceptance tests plus three verified DMG boot-state ROMs")
    parser.add_argument("--only", help="substring filter within the selected ROM set")
    parser.add_argument("--max-cycles", type=int, default=100_000_000)
    parser.add_argument("--timeout", type=int, default=60)
    args = parser.parse_args()
    binary = args.binary.resolve()
    if args.max_cycles <= 0 or args.timeout <= 0:
        parser.error("cycle and timeout bounds must be positive")
    output = (args.output or ROOT / "artifacts" / f"mooneye-{time.time_ns()}").resolve()
    output.mkdir(parents=True, exist_ok=False)
    external = ROOT / "roms" / "mooneye"
    source_external = ROOT / "roms" / "mooneye-source"
    manifest = json.loads((external / "manifest.json").read_text())
    source_manifest = json.loads((source_external / "manifest.json").read_text())
    # Audit the entire fetched oracle, including the original assertions and
    # symbol files, rather than trusting only the ROM names selected below.
    for base, inventory in ((external, manifest), (source_external, source_manifest)):
        for name, item in inventory["files"].items():
            if sha(base / name) != item["sha256"]:
                raise RuntimeError(f"external oracle integrity mismatch: {base / name}")
    manifest_hashes = {base / "manifest.json": sha(base / "manifest.json")
                       for base in (external, source_external)}
    excluded = []
    if args.all_acceptance:
        names = []
        for name in sorted(manifest["files"]):
            if not name.startswith("acceptance/") or not name.endswith(".gb"):
                continue
            reason = exclusion(name)
            if reason:
                excluded.append({"rom": name, "reason": reason})
            else:
                names.append(name)
        if names != list(DMG_ACCEPTANCE):
            missing = sorted(set(DMG_ACCEPTANCE) - set(names))
            unexpected = sorted(set(names) - set(DMG_ACCEPTANCE))
            raise RuntimeError(f"pinned DMG selection changed: missing={missing}, unexpected={unexpected}")
        if len(names) != 65 or not set(REQUESTED).issubset(names):
            raise RuntimeError("full campaign must include all ten requested ROMs and all 65 selected DMG acceptance ROMs")
    else:
        names = list(REQUESTED)
    if args.only:
        names = [name for name in names if args.only in name]
    if not names:
        raise RuntimeError("no ROMs selected")
    for name in names:
        if name not in manifest["files"]:
            raise RuntimeError(f"required original ROM missing: {name}")
    sources = {path.relative_to(ROOT).as_posix(): sha(path)
               for directory in ("include", "src", "tests", "tools")
               for path in (ROOT / directory).rglob("*")
               if path.is_file() and "__pycache__" not in path.parts}
    sources["Makefile"] = sha(ROOT / "Makefile")
    binary_hash = sha(binary)
    summary = {"passed": False, "platform": platform.platform(), "target": "DMG CPU A/B/C",
               "protocol": "official LD B,B with Fibonacci registers, not a PC=0000 heuristic",
               "scope": "62 DMG runtime and three boot-state acceptance ROMs" if args.all_acceptance else "ten requested acceptance ROMs",
               "binary": str(binary), "binary_sha256": binary_hash,
               "rom_manifest": manifest, "original_source_manifest": source_manifest,
               "sources": sources, "selected": names, "excluded": excluded, "tests": []}
    started = time.monotonic()
    dump_json(output / "summary.json", summary)
    for name in names:
        label = re.sub(r"[^a-zA-Z0-9_-]", "_", name.removesuffix(".gb"))
        report = output / f"{label}.json"
        trace = output / f"{label}.trace.txt"
        stderr = output / f"{label}.stderr.txt"
        command = [str(binary), str(external / name), "--protocol", "mooneye",
                   "--max-cycles", str(args.max_cycles), "--timeout", str(args.timeout),
                   "--report", str(report), "--trace-out", str(trace), "--trace-capacity", "65536",
                   "--frame-out", str(output / f"{label}.pgm")]
        print(f"RUN {name}", flush=True)
        before = time.monotonic()
        error = None
        with (output / f"{label}.stdout.txt").open("w") as out, stderr.open("w") as err:
            try:
                process = subprocess.run(command, stdout=out, stderr=err, timeout=args.timeout + 10, check=False)
                exit_code = process.returncode
            except subprocess.TimeoutExpired:
                exit_code = 124
                error = "external wall-clock timeout; no pass inferred"
            except OSError as failure:
                exit_code = 127
                error = str(failure)
        try:
            data = json.loads(report.read_text())
        except (OSError, ValueError):
            data = {"status": "inconclusive_missing_report"}
        if not isinstance(data, dict):
            data = {"status": "inconclusive_invalid_report"}
        data.update({"rom": name, "sha256": manifest["files"][name]["sha256"],
                     "command": command, "exit_code": exit_code, "seconds": time.monotonic() - before})
        if error:
            data["runner_error"] = error
        data["passed"] = data.get("status") == "passed" and exit_code == 0
        summary["tests"].append(data)
        print(f"{'PASS' if data['passed'] else 'FAIL'} {name}: {data.get('status')} "
              f"({data.get('t_cycles', '?')} T-cycles)", flush=True)
        if not data["passed"]:
            # Complete logs remain on disk. Keep terminal output useful when a
            # fault recurs thousands of times inside an emulated test loop.
            tail(trace if trace.exists() else stderr)
        dump_json(output / "summary.json", summary)
    summary["source_unchanged"] = (all((ROOT / name).is_file() and sha(ROOT / name) == value
                                      for name, value in sources.items())
                                      and binary.is_file() and sha(binary) == binary_hash)
    summary["oracles_unchanged"] = all(sha(path) == digest for path, digest in manifest_hashes.items()) and all(sha(base / name) == item["sha256"]
        for base, inventory in ((external, manifest), (source_external, source_manifest))
        for name, item in inventory["files"].items())
    summary["seconds"] = time.monotonic() - started
    summary["passed_count"] = sum(test["passed"] for test in summary["tests"])
    summary["passed"] = (summary["source_unchanged"] and summary["oracles_unchanged"]
                         and all(test["passed"] for test in summary["tests"]))
    dump_json(output / "summary.json", summary)
    print(f"{'PASS' if summary['passed'] else 'FAIL'} {summary['passed_count']}/{len(names)} ROMs; {output}", flush=True)
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
