#!/usr/bin/env python3
"""Build once and verify the same immutable DMG executable against every oracle."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import time

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inventory():
    paths = [ROOT / "Makefile"]
    if (ROOT / "CMakeLists.txt").is_file():
        paths.append(ROOT / "CMakeLists.txt")
    if (ROOT / "matcha_gym.py").is_file():
        paths.append(ROOT / "matcha_gym.py")
    for directory in ("include", "src", "tests", "tools", "cmake", ".github"):
        paths.extend(p for p in (ROOT / directory).rglob("*")
                     if p.is_file() and "__pycache__" not in p.parts)
    return {p.relative_to(ROOT).as_posix(): sha(p) for p in sorted(paths)}


def archive_sources(sources, destination):
    # Read each file once, then archive the exact bytes whose hash was checked.
    # Later source edits still invalidate the campaign's final integrity gate.
    with tarfile.open(destination, "w:gz") as archive:
        for name, expected in sources.items():
            source = ROOT / name
            data = source.read_bytes()
            if hashlib.sha256(data).hexdigest() != expected:
                raise RuntimeError(f"source changed before build snapshot: {name}")
            member = tarfile.TarInfo(name)
            member.size = len(data)
            member.mode = source.stat().st_mode & 0o777
            archive.addfile(member, io.BytesIO(data))
    with tarfile.open(destination, "r:gz") as archive:
        restored = {member.name: hashlib.sha256(archive.extractfile(member).read()).hexdigest()
                    for member in archive.getmembers()}
    if restored != sources:
        raise RuntimeError("tested-source archive did not preserve the source inventory")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    output = (args.output or ROOT / "artifacts" / f"verified-{time.time_ns()}").resolve()
    output.mkdir(parents=True, exist_ok=False)
    summary = {"passed": False, "platform": platform.platform(), "sources": inventory(),
               "commands": [], "suites": {}, "scope": "DMG CPU A/B/C, post-boot"}
    report = output / "summary.json"

    def save():
        temporary = report.with_suffix(".json.tmp")
        temporary.write_text(json.dumps(summary, indent=2) + "\n")
        temporary.replace(report)

    def run(label, command):
        print(f"RUN {label}", flush=True)
        started = time.monotonic()
        with (output / f"{label}.log").open("w") as log:
            try:
                result = subprocess.run(command, cwd=ROOT, stdout=log,
                                        stderr=subprocess.STDOUT, timeout=1800, check=False)
                code = result.returncode
            except subprocess.TimeoutExpired:
                code = 124
        summary["commands"].append({"name": label, "command": command, "exit_code": code,
                                    "seconds": time.monotonic() - started})
        save()
        print(f"{'PASS' if code == 0 else 'FAIL'} {label}; log: {output / (label + '.log')}", flush=True)
        return code == 0

    started = time.monotonic()
    save()
    archive_sources(summary["sources"], output / "tested-source.tar.gz")
    summary["source_archive_sha256"] = sha(output / "tested-source.tar.gz")
    save()
    if not run("build", ["make", "-B", "-j4", "all"]):
        return 1  # Never run an old executable after a failed rebuild.
    summary["binary_sha256"] = sha(ROOT / "build/dmg")
    shutil.copy2(ROOT / "build/dmg", output / "dmg")
    if sha(output / "dmg") != summary["binary_sha256"]:
        raise RuntimeError("preserved executable differs from the built executable")
    save()
    run("toolchain", ["clang++", "--version"])
    run("unit", ["make", "test"])
    run("sanitizer", ["make", "sanitize"])
    run("harness-io", [sys.executable, str(ROOT / "tools/verify_harness_io.py"),
                       "--output", str(output / "harness-io")])
    suites = (
        ("mooneye", "verify_mooneye.py", ["--all-acceptance"]),
        ("oam", "verify_oam.py", []),
        ("blargg", "verify.py", ["--extra"]),
        ("sound", "verify.py", ["--sound"]),
        ("acid2", "verify_ppu.py", []),
        ("mealybug", "verify_mealybug.py", []),
    )
    for label, script, options in suites:
        run(label, [sys.executable, str(ROOT / "tools" / script), *options,
                    "--output", str(output / label)])
        try:
            result = json.loads((output / label / "summary.json").read_text())
        except (OSError, ValueError):
            result = {"passed": False, "error": "missing or invalid suite report"}
        summary["suites"][label] = result
        save()
    summary["sources_unchanged"] = inventory() == summary["sources"]
    summary["binary_unchanged"] = sha(ROOT / "build/dmg") == summary["binary_sha256"]
    summary["archive_unchanged"] = (
        sha(output / "tested-source.tar.gz") == summary["source_archive_sha256"]
        and sha(output / "dmg") == summary["binary_sha256"])
    summary["seconds"] = time.monotonic() - started
    summary["passed"] = (summary["sources_unchanged"] and summary["binary_unchanged"]
                         and summary["archive_unchanged"]
                         and all(c["exit_code"] == 0 for c in summary["commands"])
                         and all(s.get("passed") is True for s in summary["suites"].values()))
    save()
    print(f"{'PASS' if summary['passed'] else 'FAIL'} complete campaign; {report}", flush=True)
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
