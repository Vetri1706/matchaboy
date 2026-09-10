#!/usr/bin/env python3
"""Exercise output failures against the real harness and an original Mooneye ROM."""
import argparse
import hashlib
import json
from pathlib import Path
import resource
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]


def limit_child_file_size():
    # The kernel rejects real writes to regular files with EFBIG. Pipes used
    # for stdout/stderr are unaffected; no fake stream or emulator is involved.
    signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
    resource.setrlimit(resource.RLIMIT_FSIZE, (1, 1))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    output = (args.output or ROOT / "artifacts" / f"harness-io-{time.time_ns()}").resolve()
    output.mkdir(parents=True, exist_ok=False)
    binary = ROOT / "build/dmg"
    rom = ROOT / "roms/mooneye/acceptance/bits/mem_oam.gb"
    manifest = json.loads((ROOT / "roms/mooneye/manifest.json").read_text())
    rom_sha = hashlib.sha256(rom.read_bytes()).hexdigest()
    if rom_sha != manifest["files"]["acceptance/bits/mem_oam.gb"]["sha256"]:
        raise RuntimeError("original Mooneye ROM integrity mismatch")
    command = [str(binary), str(rom), "--protocol", "mooneye",
               "--max-cycles", "10000000", "--timeout", "30"]
    control = subprocess.run(command, capture_output=True, timeout=40, check=False)
    tests = [{"case": "normal execution", "passed": control.returncode == 0,
              "exit_code": control.returncode}]
    (output / "control.stdout.txt").write_bytes(control.stdout)
    (output / "control.stderr.txt").write_bytes(control.stderr)
    for kind, option in (("frame", "--frame-out"), ("trace", "--trace-out"), ("report", "--report")):
        result = subprocess.run(command + [option, str(output / (kind + ".partial"))],
                                capture_output=True, timeout=40, check=False,
                                preexec_fn=limit_child_file_size)
        error = f"cannot write {kind} output".encode()
        tests.append({"case": kind + " kernel write rejection", "exit_code": result.returncode,
                      "passed": result.returncode == 2 and error in result.stderr})
        (output / (kind + ".stdout.txt")).write_bytes(result.stdout)
        (output / (kind + ".stderr.txt")).write_bytes(result.stderr)
    summary = {"passed": all(test["passed"] for test in tests), "tests": tests,
               "rom_sha256": rom_sha, "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
               "mechanism": "POSIX RLIMIT_FSIZE, real filesystem write failures"}
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(f"{'PASS' if summary['passed'] else 'FAIL'} harness output error handling: {output}")
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
