#!/usr/bin/env python3
"""Run original ROM and real-socket checks against an explicitly selected build."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    binaries = args.binary_dir.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    suffix = ".exe" if sys.platform == "win32" else ""
    extension = "dll" if sys.platform == "win32" else "dylib" if sys.platform == "darwin" else "so"
    emulator, netplay = binaries / ("dmg" + suffix), binaries / ("netplay" + suffix)
    library = binaries / ("libmatcha." + extension)
    paths = [emulator, netplay, library]
    before = {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}
    report = {"passed": False, "platform": sys.platform, "binaries": before, "checks": []}
    environment = dict(os.environ, MATCHA_LIBRARY=str(library), MATCHA_NETPLAY=str(netplay))

    def save():
        (output / "summary.json").write_text(json.dumps(report, indent=2) + "\n")

    def run(name, arguments):
        print("RUN " + name, flush=True)
        started = time.monotonic()
        with (output / (name + ".log")).open("w", encoding="utf-8") as log:
            completed = subprocess.run([sys.executable, *arguments], cwd=ROOT, env=environment,
                                       stdout=log, stderr=subprocess.STDOUT, check=False)
        report["checks"].append({"name": name, "exit_code": completed.returncode,
                                 "seconds": time.monotonic() - started})
        save()
        if completed.returncode:
            print((output / (name + ".log")).read_text(encoding="utf-8"), flush=True)
            raise RuntimeError("failed " + name)

    save()
    try:
        run("python-gymnasium", ["-m", "unittest", "discover", "-s", "tests", "-p", "test_matcha_gym.py"])
        for name, script, options in (
            ("blargg", "verify.py", ["--extra"]),
            ("sound", "verify.py", ["--sound"]),
            ("mooneye", "verify_mooneye.py", ["--all-acceptance"]),
            ("oam", "verify_oam.py", []),
            ("acid2", "verify_ppu.py", []),
            ("mealybug", "verify_mealybug.py", []),
        ):
            run(name, ["tools/" + script, "--binary", str(emulator),
                       "--output", str(output / name), *options])
            verdict = json.loads((output / name / "summary.json").read_text())
            if verdict.get("passed") is not True:
                raise RuntimeError("missing passing original-oracle verdict: " + name)
        run("protocol-rejection", ["tools/test_netplay_protocol.py"])
        run("netplay", ["tools/verify_netplay.py", "--binary", str(netplay),
                        "--frames", "100", "--output", str(output / "netplay")])
        run("netplay-idle", ["tools/verify_netplay.py", "--binary", str(netplay),
                             "--frames", "20", "--scenario", "idle-peer",
                             "--output", str(output / "netplay-idle")])
        report["binaries_unchanged"] = all(
            hashlib.sha256(path.read_bytes()).hexdigest() == before[path.name] for path in paths)
        report["passed"] = report["binaries_unchanged"]
    except (OSError, ValueError, RuntimeError) as error:
        report["error"] = str(error)
    save()
    print(json.dumps(report, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
