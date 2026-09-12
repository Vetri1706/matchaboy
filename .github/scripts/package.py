#!/usr/bin/env python3
"""Package installed binaries, preserve executable modes, then test the extracted ZIP."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
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
    homebrew = json.loads((ROOT / "games/homebrew/manifest.json").read_text())["games"]
    if sys.platform != "win32":
        rom_directory = stage / ("MatchaAutopsy.app/Contents/Resources/Arcade" if sys.platform == "darwin" else "assets/roms")
        if {path.name for path in rom_directory.iterdir()} != {entry["rom_filename"] for entry in homebrew}:
            raise RuntimeError("staged library has missing/stale ROMs; rebuild and install into a fresh staging directory")
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
                                         ".github", "cmake", "third_party", "matcha_gym.py", "*.md"], cwd=ROOT, text=True).strip())
    # A previous packaging run may have left its manifest in the staging tree.
    # The manifest cannot include its own hash.
    if sys.platform in ("darwin", "win32") or sys.platform.startswith("linux"):
        sys.path.insert(0, str(ROOT / "tools"))
        from package_player import add_sources
        add_sources(stage)
    macos_signing = None
    manifest = stage / "BUILD_INFO.json"
    if sys.platform == "darwin":
        from verify_macos_bundle import verify_bundle, verify_tamper_detection
        # The build keeps its historical target name; the download presents
        # one clearly named app. Optional tools retain their relative paths
        # together inside Extras, including the ctypes library and wrapper.
        bundle = stage / "Matchaboy.app"
        (stage / "MatchaAutopsy.app").rename(bundle)
        extras = stage / "Extras"
        extras.mkdir()
        for entry in list(stage.iterdir()):
            if entry not in (bundle, extras):
                entry.rename(extras / entry.name)
        shutil.copyfile(ROOT / "tools/macos_start_here.txt", stage / "Start Here.txt")
        manifest = extras / "BUILD_INFO.json"
        binary = bundle / "Contents/MacOS/MatchaAutopsy"
        # The hashes above bind this input to the tested build. The linker's
        # arm64 signature covers only the executable, not the completed bundle.
        # Seal resources after installation, then record the signing-only hash
        # change separately. No certificate, account or keychain is modified.
        tested_hash = sha(binary)
        subprocess.run(["/usr/bin/codesign", "--force", "--sign", "-",
                        "--timestamp=none", str(bundle)], check=True)
        verify_bundle(bundle)
        load_commands = subprocess.check_output(
            ["/usr/bin/vtool", "-show-build", str(binary)], text=True)
        minimum = re.search(r"\bminos\s+([0-9.]+)", load_commands)
        if minimum is None:
            raise RuntimeError("cannot determine the Mac app's minimum OS version")
        macos_signing = {"identity": "ad-hoc", "notarized": False,
                         "bundle_integrity_verified": True,
                         "minimum_macos": minimum.group(1),
                         "tested_executable_sha256": tested_hash,
                         "signed_executable_sha256": sha(binary)}
    if sys.platform == "win32" or sys.platform.startswith("linux"):
        player = stage / ("Matchaboy.exe" if sys.platform == "win32" else "Matchaboy")
        if not player.is_file():
            raise RuntimeError("desktop package is missing its player executable")
        extras = stage / "Extras"
        extras.mkdir()
        keep = {player, extras}
        if sys.platform.startswith("linux"):
            keep.add(stage / "assets")
        for entry in list(stage.iterdir()):
            if entry not in keep:
                entry.rename(extras / entry.name)
        platform_guide = "windows_start_here.txt" if sys.platform == "win32" else "linux_start_here.txt"
        shutil.copyfile(ROOT / "tools" / platform_guide, stage / "Start Here.txt")
        manifest = extras / "BUILD_INFO.json"
    files = sorted(path for path in stage.rglob("*")
                   if path.is_file() and path != manifest)
    build_info = {"commit": commit, "source_dirty": dirty, "platform": platform.platform(), "architecture": platform.machine(),
                  "compiler": subprocess.check_output(["clang++", "--version"], text=True).strip(),
                  "verification": verification,
                  "files": {str(path.relative_to(stage)).replace(os.sep, "/"): sha(path) for path in files}}
    if macos_signing is not None:
        build_info["macos_signing"] = macos_signing
    manifest.write_text(json.dumps(build_info, indent=2) + "\n")
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
            # Exercise Apple's extractor as well as ZIP integrity. It preserves
            # executable modes without the chmod repair used on other POSIX OSes.
            if sys.platform == "darwin":
                subprocess.run(["/usr/bin/ditto", "-x", "-k", str(archive),
                                str(destination)], check=True)
            else:
                package.extractall(destination)
            if os.name != "nt" and sys.platform != "darwin":
                for entry in package.infolist():
                    (destination / entry.filename).chmod((entry.external_attr >> 16) & 0o777)
        installed = destination / args.name
        for name, digest in build_info["files"].items():
            if sha(installed / name) != digest: raise RuntimeError("archive member hash mismatch")
        if sys.platform == "darwin":
            if {entry.name for entry in installed.iterdir()} != {"Matchaboy.app", "Start Here.txt", "Extras"}:
                raise RuntimeError("Mac download must expose only the player, quick start and Extras")
            verify_tamper_detection(installed / "Matchaboy.app")
            print("Verified extracted Mac bundle; altered resources and Info.plist rejected")
        if sys.platform != "darwin":
            expected_entries = {"Extras", "Start Here.txt", "Matchaboy.exe" if sys.platform == "win32" else "Matchaboy"}
            if sys.platform.startswith("linux"):
                expected_entries.add("assets")
            if {entry.name for entry in installed.iterdir()} != expected_entries:
                raise RuntimeError("desktop download has unexpected top-level files")
        developer_files = installed / "Extras"
        rom = destination / "input fixture.gb"
        data = bytearray(32768)
        data[0x100:0x103] = bytes([0xC3, 0x50, 1])
        data[0x150:0x163] = bytes([0xF3, 0x31, 0xFE, 0xFF, 0x3E, 0x20, 0xE0, 0,
                                 0xF0, 0, 0x2F, 0xE6, 0x0F, 0xEA, 0, 0xC0, 0xC3, 0x58, 1])
        rom.write_bytes(data)
        executable = developer_files / ("dmg.exe" if os.name == "nt" else "dmg")
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
        subprocess.run([sys.executable, "-c", smoke, str(developer_files), str(rom)],
                       cwd=destination, env=environment, check=True)
        if sys.platform in ("darwin", "win32") or sys.platform.startswith("linux"):
            app = installed / ("Matchaboy.exe" if sys.platform == "win32" else "Matchaboy.app/Contents/MacOS/MatchaAutopsy" if sys.platform == "darwin" else "Matchaboy")
            if sys.platform == "darwin":
                # Follow the quick start exactly: install only the .app, with
                # no neighboring Extras or checkout files to satisfy lookups.
                relocated = destination / "Applications/Matchaboy.app"
                shutil.copytree(installed / "Matchaboy.app", relocated)
                verify_bundle(relocated)
                app = relocated / "Contents/MacOS/MatchaAutopsy"
            elif sys.platform.startswith("linux"):
                relocated = destination / "Applications/Matchaboy"
                relocated.mkdir(parents=True)
                shutil.copy2(app, relocated / "Matchaboy")
                shutil.copytree(installed / "assets", relocated / "assets")
                app = relocated / "Matchaboy"
            subprocess.run([str(app), str(rom), "--headless", "--frames", "2", "--capture", str(destination / "hud.png")],
                           cwd=destination, check=True, stdout=subprocess.DEVNULL)
            if not (destination / "hud.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n"):
                raise RuntimeError("packaged dashboard did not render")
            if sys.platform in ("darwin", "win32") or sys.platform.startswith("linux"):
                # Exercise bundled resources outside the checkout too. A library
                # page alone would not prove that its selected ROM can load.
                game_environment = dict(environment, MATCHA_GAME_LIBRARY=str(destination / "game library"))
                for entry in homebrew:
                    game = entry["id"]
                    capture = destination / (game + ".png")
                    subprocess.run([str(app), "--game", game, "--headless", "--frames", str(entry["test"]["boot_frames"]), "--capture", str(capture)],
                                   cwd=destination, env=game_environment, check=True, stdout=subprocess.DEVNULL)
                    if not capture.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"):
                        raise RuntimeError("packaged arcade ROM did not render: " + game)
                    if sha(destination / "game library" / entry["rom_filename"]) != entry["sha256"]:
                        raise RuntimeError("packaged homebrew differs from the licensed release: " + game)
                if (developer_files / "licenses/homebrew/CREDITS.txt").read_bytes() != (ROOT / "games/homebrew/CREDITS.txt").read_bytes():
                    raise RuntimeError("packaged homebrew credits are missing or altered")
                subprocess.run([sys.executable, str(ROOT / "tools/test_friend_player.py"),
                                "--binary", str(app), "--output", str(destination / "friendplay-smoke")],
                               cwd=destination, check=True, stdout=subprocess.DEVNULL)
    (output / (archive.name + ".sha256")).write_text(sha(archive) + "  " + archive.name + "\n")
    print("Verified downloadable package: " + str(archive))


if __name__ == "__main__":
    main()
