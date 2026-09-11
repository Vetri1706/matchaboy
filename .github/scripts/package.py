#!/usr/bin/env python3
"""Package installed binaries, preserve executable modes, then test the extracted ZIP."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--verification", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("dist"))
    args = parser.parse_args()
    if not re.fullmatch(r"matchaboy-(windows-x64|linux-x64|macos-arm64)", args.name):
        parser.error("unknown package name")
    expected = {"matchaboy-windows-x64": ("win32", {"amd64", "x86_64"}),
                "matchaboy-linux-x64": ("linux", {"x86_64", "amd64"}),
                "matchaboy-macos-arm64": ("darwin", {"arm64", "aarch64"})}
    operating_system, architectures = expected[args.name]
    if sys.platform != operating_system or platform.machine().lower() not in architectures:
        raise RuntimeError("runner architecture does not match package label")
    stage, output = args.stage.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    verification = json.loads(args.verification.read_text())
    if verification.get("passed") is not True:
        raise RuntimeError("refusing to package an unverified build")
    for name, digest in verification["binaries"].items():
        if sha(stage / name) != digest:
            # macOS install rewrites a dylib's install name; verify the ABI on
            # the extracted package below, and record both exact hashes.
            if not (sys.platform == "darwin" and name == "libmatcha.dylib"):
                raise RuntimeError("installed executable differs from tested build: " + name)
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=all", "--",
                                         "Makefile", "CMakeLists.txt", "include", "src", "tests", "tools", "games", "assets",
                                         ".github", "matcha_gym.py"], cwd=ROOT, text=True).strip())
    # A previous packaging run may have left its manifest in the staging tree.
    # The manifest cannot include its own hash.
    if sys.platform == "win32":
        sys.path.insert(0, str(ROOT / "tools"))
        from package_player import add_sources
        add_sources(stage)
    files = sorted(path for path in stage.rglob("*")
                   if path.is_file() and path != stage / "BUILD_INFO.json")
    build_info = {"commit": commit, "source_dirty": dirty, "platform": platform.platform(), "architecture": platform.machine(),
                  "compiler": subprocess.check_output(["clang++", "--version"], text=True).strip(),
                  "verification": verification,
                  "files": {str(path.relative_to(stage)).replace(os.sep, "/"): sha(path) for path in files}}
    (stage / "BUILD_INFO.json").write_text(json.dumps(build_info, indent=2) + "\n")
    archive = output / (args.name + ".zip")
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as package:
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                package.write(path, Path(args.name) / path.relative_to(stage))
    # Exercise exactly what a user downloads from a different, space-containing
    # directory, with the source checkout and build folder absent from lookup.
    with tempfile.TemporaryDirectory(prefix="matchaboy download ") as temporary:
        destination = Path(temporary)
        with zipfile.ZipFile(archive) as package:
            for entry in package.infolist():
                relative = Path(entry.filename)
                if relative.is_absolute() or ".." in relative.parts:
                    raise RuntimeError("unsafe package member")
            package.extractall(destination)
            if os.name != "nt":
                for entry in package.infolist():
                    (destination / entry.filename).chmod((entry.external_attr >> 16) & 0o777)
        installed = destination / args.name
        for name, digest in build_info["files"].items():
            if sha(installed / name) != digest: raise RuntimeError("archive member hash mismatch")
        rom = destination / "input fixture.gb"
        data = bytearray(32768)
        data[0x100:0x103] = bytes([0xC3, 0x50, 1])
        data[0x150:0x163] = bytes([0xF3, 0x31, 0xFE, 0xFF, 0x3E, 0x20, 0xE0, 0,
                                 0xF0, 0, 0x2F, 0xE6, 0x0F, 0xEA, 0, 0xC0, 0xC3, 0x58, 1])
        rom.write_bytes(data)
        executable = installed / ("dmg.exe" if os.name == "nt" else "dmg")
        subprocess.run([str(executable), str(rom), "--frames", "2", "--report", str(destination / "run.json")],
                       cwd=destination, check=True, stdout=subprocess.DEVNULL)
        # A fresh process proves package discovery without checkout imports.
        # Its exit also unloads the DLL before Windows deletes the extraction.
        smoke = """import sys
from pathlib import Path
sys.path.insert(0, sys.argv[1])
import matcha_gym
assert Path(matcha_gym.__file__).resolve().parent == Path(sys.argv[1]).resolve()
with matcha_gym.NativeBatch(sys.argv[2], 16) as batch:
    batch.watch(0xC000, delta=False)
    rewards, dones = batch.step(list(range(16)))
    assert list(rewards) == list(range(16)) and not dones.any()
"""
        environment = {key: value for key, value in os.environ.items()
                       if key not in ("MATCHA_LIBRARY", "PYTHONPATH")}
        subprocess.run([sys.executable, "-c", smoke, str(installed), str(rom)],
                       cwd=destination, env=environment, check=True)
        if sys.platform in ("darwin", "win32"):
            app = installed / ("Matchaboy.exe" if sys.platform == "win32" else "MatchaAutopsy.app/Contents/MacOS/MatchaAutopsy")
            subprocess.run([str(app), str(rom), "--headless", "--frames", "2", "--capture", str(destination / "hud.png")],
                           cwd=destination, check=True, stdout=subprocess.DEVNULL)
            if not (destination / "hud.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n"):
                raise RuntimeError("packaged dashboard did not render")
    (output / (archive.name + ".sha256")).write_text(sha(archive) + "  " + archive.name + "\n")
    print("Verified downloadable package: " + str(archive))


if __name__ == "__main__":
    main()
