#!/usr/bin/env python3
"""Verify the native Mac player using real cartridges and a relocated app bundle.

Headless checks do not manipulate the desktop. --window-tests additionally asks
the application to create its own native window, read back its GPU framebuffer,
and close. Interactive menu/keyboard and speaker playback checks are separate.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile

from verify_autopsy import compare, png_rgb

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def portable_pixels(path):
    """Read the app's uncompressed PPM or the core CLI's uncompressed PGM."""
    data = path.read_bytes()
    offset = 0
    tokens = []
    while len(tokens) != 4:
        while offset < len(data) and data[offset] in b" \t\r\n":
            offset += 1
        if offset < len(data) and data[offset] == ord("#"):
            offset = data.index(b"\n", offset) + 1
            continue
        start = offset
        while offset < len(data) and data[offset] not in b" \t\r\n":
            offset += 1
        require(start != offset, f"Invalid portable image header: {path}")
        tokens.append(data[start:offset])
    require(tokens[0] in (b"P5", b"P6") and tokens[3] == b"255",
            f"Unsupported portable image: {path}")
    require(offset < len(data) and data[offset] in b" \t\r\n",
            f"Missing portable image separator: {path}")
    offset += 2 if data[offset:offset + 2] == b"\r\n" else 1
    width, height = int(tokens[1]), int(tokens[2])
    channels = 1 if tokens[0] == b"P5" else 3
    pixels = data[offset:]
    require(len(pixels) == width * height * channels, f"Truncated portable image: {path}")
    if channels == 1:
        pixels = bytes(value for pixel in pixels for value in (pixel, pixel, pixel))
    return width, height, pixels


def lcd_capture(path, state):
    """Check every native LCD pixel against the corresponding displayed pixel."""
    width, height, pixels = portable_pixels(Path(str(path) + ".lcd.ppm"))
    require((width, height) == (state["lcd_native_width"], state["lcd_native_height"]),
            "Raw framebuffer dimensions disagree with machine metadata")
    canvas_width, canvas_height, rendered = png_rgb(path)
    require((canvas_width, canvas_height) == (state["width"], state["height"]),
            "Capture dimensions disagree with metadata")
    rectangle = state.get("lcd_rect")
    if rectangle:
        left, top, display_width, display_height = rectangle
        require(left >= 0 and top >= 0 and display_width > 0 and display_height > 0 and
                left + display_width <= canvas_width and top + display_height <= canvas_height,
                "LCD rectangle falls outside the capture")
        mismatches = 0
        for y in range(height):
            output_y = int(top + (y + .5) * display_height / height)
            for x in range(width):
                output_x = int(left + (x + .5) * display_width / width)
                expected = (y * width + x) * 3
                actual = (output_y * canvas_width + output_x) * 3
                mismatches += pixels[expected:expected + 3] != rendered[actual:actual + 3]
        require(mismatches == 0, f"{path.name}: {mismatches} LCD pixels differ from the core framebuffer")
    return pixels


def gba_fixture():
    # Authored ARM7TDMI cartridge: increments SRAM[0] at boot, reads KEYINPUT,
    # and fills mode 3 red, or green/blue/white for A/L/R. No external BIOS.
    code = bytes.fromhex(
        "0e04a0e30010d0e5011081e20010c0e540509fe5b030d5e11f10a0e3010013e3"
        "34109f05020c13e330109f05010c13e32c109f050604a0e3962ca0e3b210c0e0"
        "012052e2fcffff1a0103a0e314109fe5b010c0e1eeffffea30010004e0030000"
        "007c0000ff7f0000030400005352414d5f56313133")
    rom = bytearray(0xC0)
    rom[:4] = bytes.fromhex("2e0000ea")
    rom[0xB2] = 0x96
    return rom + code


def bundle_path(path):
    resolved = path.resolve()
    if resolved.suffix == ".app" and resolved.is_dir():
        return resolved
    for parent in resolved.parents:
        if parent.suffix == ".app":
            return parent
    raise RuntimeError("--binary must identify a .app bundle or its executable")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--dmg", type=Path, required=True, help="independent DMG core CLI framebuffer oracle")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--window-tests", action="store_true", help="also verify native GPU readbacks")
    args = parser.parse_args()
    if sys.platform != "darwin":
        parser.error("requires macOS")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    report = {"passed": False, "checks": [], "games": [], "commands": [],
              "scope": "Real cartridge execution, framebuffer comparisons, isolated bundle resources, "
                       "GB/GBA Inspector state and native captures when requested. "
                       "Interactive keyboard/menu tests and speaker playback are separate."}
    try:
        source_bundle = bundle_path(args.binary)
        with (source_bundle / "Contents/Info.plist").open("rb") as file:
            executable_name = plistlib.load(file)["CFBundleExecutable"]
        original_executable = source_bundle / "Contents/MacOS" / executable_name
        report["binary_sha256"] = sha(original_executable)
        report["dmg_sha256"] = sha(args.dmg)
        manifests = [json.loads((ROOT / "games" / system / "manifest.json").read_text())["games"]
                     for system in ("gb", "gba")]
        games = [game for manifest in manifests for game in manifest]
        require(len(games) == 10 and len({game["id"] for game in games}) == 10,
                "Expected ten distinct original arcade cartridges")
        for game in games:
            require(sha(ROOT / game["rom"]) == game["sha256"], f"Source ROM hash mismatch: {game['id']}")

        with tempfile.TemporaryDirectory(prefix="Matchaboy Mac 移动 ") as temporary:
            isolated = Path(temporary)
            moved = isolated / "Matchaboy.app"
            shutil.copytree(source_bundle, moved)
            binary = moved / "Contents/MacOS" / executable_name
            environment = {key: value for key, value in os.environ.items()
                           if not key.startswith(("MATCHA", "DYLD_")) and key != "PYTHONPATH"}
            environment["PATH"] = "/usr/bin:/bin:/usr/sbin:/sbin"
            for game in games:
                candidates = list((moved / "Contents/Resources").rglob(Path(game["rom"]).name))
                require(len(candidates) == 1 and sha(candidates[0]) == game["sha256"],
                        f"Relocated bundle lacks the exact original cartridge: {game['id']}")

            def run(name, arguments, expected_success=True):
                command = [str(binary), *map(str, arguments)]
                with (output / f"{name}.log").open("w") as log:
                    result = subprocess.run(command, cwd=isolated, env=environment,
                                            stdout=log, stderr=subprocess.STDOUT, timeout=30)
                report["commands"].append({"name": name, "command": command, "exit_code": result.returncode})
                require((result.returncode == 0) == expected_success,
                        f"{name} returned {result.returncode}; see {name}.log")

            def capture(name, arguments=(), window=False):
                path = output / f"{name}.png"
                run(name, [*arguments, "--window-test" if window else "--headless", "--capture", path])
                state = json.loads(Path(str(path) + ".json").read_text())
                require(state["gpu_readback"] is window, f"{name}: incorrect GPU readback evidence")
                require(state["paused"], f"{name}: deterministic capture must be paused")
                return path, state

            library_path, library = capture("library", ["--frames", 0])
            require(library["platform"] == "library" and library["library_view"] and
                    library["library_game"] == -1 and library["frames"] == 0,
                    "Launching without a ROM did not open the real arcade library")
            width, height, library_rgb = png_rgb(library_path)
            require(width >= 960 and height >= 600 and len(set(library_rgb)) > 64,
                    "Library capture is empty or undersized")
            report["checks"].append("Relocated Unicode-path app opens its ten-game library without source-tree resources")
            title_hashes = set()
            for index, game in enumerate(games):
                title_path, title = capture(game["id"] + "-title", ["--game", game["id"], "--frames", 60])
                require(title["platform"] == game["system"].lower() and not title["library_view"] and
                        not title["inspector_view"] and title["library_game"] == index and 0 < title["frames"] <= 60,
                        f"Wrong game or view for {game['id']}")
                title_rgb = lcd_capture(title_path, title)
                title_hashes.add(hashlib.sha256(title_rgb).hexdigest())
                colors = len(set(zip(title_rgb[::3], title_rgb[1::3], title_rgb[2::3])))
                require(colors >= (3 if index < 5 else 5), f"Blank or incomplete title: {game['id']}")
                play_path, play = capture(game["id"] + "-play", ["--game", game["id"], "--frames", 60,
                                                                    "--buttons", 128, "--input-frames", 12])
                # A scene transition can also turn the LCD off while uploading
                # its tile map. Observe physical PPU frames, without inventing
                # a frame for each bounded host quantum.
                require(0 < play["frames"] - title["frames"] <= 12 and play["buttons"] == 128,
                        f"Start input did not advance {game['id']}")
                play_rgb = lcd_capture(play_path, play)
                changes = sum(title_rgb[i:i + 3] != play_rgb[i:i + 3] for i in range(0, len(title_rgb), 3))
                require(changes > 50, f"Start did not change cartridge display: {game['id']}")
                if index < 5:
                    # Separate CLI/core instance: prove UI scaling, palette and frame
                    # selection agree with the unchanged headless DMG harness.
                    oracle = output / (game["id"] + "-oracle.pgm")
                    # The ROM can disable the LCD during boot, so one bounded
                    # player frame quantum does not always emit a PPU frame.
                    command = [str(args.dmg.resolve()), str(ROOT / game["rom"]), "--frames", str(title["frames"]),
                               "--frame-out", str(oracle), "--report", str(oracle) + ".json"]
                    with (output / (game["id"] + "-oracle.log")).open("w") as log:
                        subprocess.run(command, cwd=isolated, stdout=log, stderr=subprocess.STDOUT,
                                       timeout=20, check=True)
                    oracle_state = json.loads(Path(str(oracle) + ".json").read_text())
                    require(title["cycles"] == oracle_state["t_cycles"] and
                            title["instructions"] == oracle_state["instructions"],
                            f"Mac player and independent DMG CLI stopped at different states: {game['id']}")
                    require(portable_pixels(oracle)[2] == title_rgb,
                            f"Mac player does not match independent DMG framebuffer: {game['id']}")
                report["games"].append({"id": game["id"], "system": game["system"], "passed": True,
                                        "rom_sha256": game["sha256"], "title_colors": colors,
                                        "start_changed_pixels": changes, "frames": play["frames"],
                                        "input_frame_quanta": 12,
                                        "input_ppu_frames": play["frames"] - title["frames"]})
            require(len(title_hashes) == 10, "Arcade games did not produce ten distinct title framebuffers")
            report["checks"].append("All ten original cartridges display distinct titles, accept Start and render gameplay")
            report["checks"].append("Every displayed title/play LCD pixel matches raw core RGB; five GB titles also match separate core CLI runs")

            reference = json.loads((output / "matcha-garden-title.png.json").read_text())
            gray_rgb = portable_pixels(output / "matcha-garden-title.png.lcd.ppm")[2]
            green_path, green_state = capture("green-palette", ["--game", "matcha-garden", "--frames", 60, "--green"])
            green_rgb = lcd_capture(green_path, green_state)
            mapping = {}
            for i in range(0, len(gray_rgb), 3):
                original, tinted = gray_rgb[i:i + 3], green_rgb[i:i + 3]
                if original in mapping:
                    require(mapping[original] == tinted, "Green palette changed the source shade map")
                else:
                    mapping[original] = tinted
            require(gray_rgb != green_rgb and len(set(mapping.values())) == len(mapping),
                    "Green palette did not tint distinct hardware shades")
            hidden_path, hidden_state = capture("hidden-controls", ["--game", "matcha-garden", "--frames", 60,
                                                                    "--hide-controls"])
            require(lcd_capture(hidden_path, hidden_state) == gray_rgb and
                    hidden_state["lcd_rect"] != reference["lcd_rect"],
                    "Hiding controls did not reposition the intact LCD")
            for state in (green_state, hidden_state):
                for key in ("frames", "cycles", "instructions"):
                    require(state[key] == reference[key], f"Presentation setting changed {key}")
            report["checks"].append("Green palette and hidden controls change presentation while preserving hardware state and LCD shade ordering")

            for game in (games[2], games[5]):
                states, panels = [], set()
                for tab in range(4):
                    path, state = capture(f"{game['system'].lower()}-inspector-{tab}",
                                          ["--game", game["id"], "--frames", 60, "--inspector", "--tab", tab])
                    require(state["inspector_view"] and state["inspector_tab"] == tab and 0 < state["frames"] <= 60,
                            "Inspector failed to select the requested panel or altered frame count")
                    if tab == 0:
                        lcd_capture(path, state)
                    states.append(state)
                    panels.add(sha(path))
                require(len(panels) == 4, "Inspector tabs render identical panels")
                invariant_keys = ("frames", "cycles", "instructions", "ly", "dot", "mode") if game["system"] == "GB" else ("frames", "pc", "cpsr", "dispcnt")
                for key in invariant_keys:
                    require(len({state[key] for state in states}) == 1, f"Inspector rendering changed {key}")
                if game["system"] == "GBA":
                    require(states[0]["dispcnt"] & 7 == 4 and states[0]["memory_base"] == 0x02000000,
                            "GBA Inspector did not report the cartridge's Mode 4 display/EWRAM")
            report["checks"].append("GB and GBA Video/CPU/Memory/Audio panels render distinct views with invariant machine state")

            _, fifo = capture("active-fifo", ["--game", "matcha-garden", "--frames", 60,
                                               "--inspector", "--tab", 0, "--line", 48, "--dot", 115])
            # CLI positioning stops at a retired instruction boundary, matching
            # the existing native probe contract, not at a forged CPU state.
            require(fifo["ly"] == 48 and 115 <= fifo["dot"] < 139 and fifo["mode"] == 3 and fifo["fifo_depth"] > 0,
                    "Video Inspector did not capture an active mode-three FIFO")
            report["checks"].append("Video Inspector captures the actual PPU at line 48, instruction boundary dot 115..138, with an active pixel FIFO")

            for label, buttons, color in (("red", 0, (255, 0, 0)), ("green", 16, (0, 255, 0)),
                                           ("blue", 256, (0, 0, 255)), ("white", 512, (255, 255, 255))):
                rom = isolated / f"hardware {label} 测.gba"
                rom.write_bytes(gba_fixture())
                path, state = capture("gba-key-" + label, [rom, "--frames", 10, "--buttons", buttons])
                pixels = lcd_capture(path, state)
                require(pixels == bytes(color) * (240 * 160), f"Incorrect GBA KEYINPUT color for {label}")
                require(state["dispcnt"] == 0x403, "Authored GBA fixture hardware mode differs")
                save = rom.with_suffix(".matchaboy.sav")
                require(save.exists() and save.read_bytes()[0] == 0, "Clean headless close did not persist GBA SRAM")
                if label == "red":
                    capture("gba-save-reload", [rom, "--frames", 10])
                    require(save.read_bytes()[0] == 1, "Second boot did not read/increment persisted SRAM")
            report["checks"].append("Authored GBA ROM proves A/L/R hardware input, exact RGB output, Unicode ROM paths and SRAM persistence")

            missing = isolated / "missing.gb"
            tiny = isolated / "invalid.gb"
            tiny.write_bytes(b"not a ROM")
            color_only = isolated / "color-only.gb"
            color_rom = bytearray(32768)
            color_rom[0x143] = 0xC0
            color_only.write_bytes(color_rom)
            for label, arguments in (("missing-rom", [missing]), ("invalid-rom", [tiny]),
                                      ("unsupported-color-only", [color_only]),
                                      ("invalid-game", ["--game", "no-such-game"]),
                                      ("invalid-tab", ["--game", "matcha-garden", "--tab", 4]),
                                      ("invalid-frames", ["--frames", -1]),
                                      ("invalid-frames-suffix", ["--frames", "12bad"]),
                                      ("overflow-frames", ["--frames", "18446744073709551616"]),
                                      ("invalid-buttons", ["--buttons", 1024]),
                                      ("invalid-dot", ["--game", "matcha-garden", "--dot", 456])):
                run(label, [*arguments, "--headless", "--capture", output / (label + ".png")], False)
            run("invalid-capture", ["--headless", "--capture", isolated / "missing" / "capture.png"], False)
            resource = next((moved / "Contents/Resources").rglob("matcha-garden.gb"))
            absent = resource.with_suffix(".temporarily-absent")
            resource.rename(absent)
            try:
                run("missing-bundled-resource", ["--game", "matcha-garden", "--headless",
                                                  "--capture", output / "missing-resource.png"], False)
            finally:
                absent.rename(resource)
            report["checks"].append("Missing/malformed ROMs, unknown games, invalid ranges and unwritable captures fail explicitly")
            report["checks"].append("A missing bundled cartridge fails instead of silently using a cached or source-tree ROM")

            if args.window_tests:
                report["gpu_comparisons"] = []
                for name, arguments in (("library", ["--frames", 0]),
                                        ("gb-player", ["--game", "matcha-garden", "--frames", 60]),
                                        ("gba-player", ["--game", "drift-circuit", "--frames", 60])):
                    headless, expected = capture(name + "-cpu", arguments)
                    gpu, actual = capture(name + "-gpu", arguments, window=True)
                    for key in ("platform", "frames", "library_view", "inspector_view", "library_game"):
                        require(expected[key] == actual[key], f"Native window changed {key}")
                    compared = compare(headless, gpu)
                    require(compared["passed"], f"Native GPU capture diverges from CPU render: {name}")
                    report["gpu_comparisons"].append({"view": name, **compared})
                report["checks"].append("Native library, GB player and GBA player GPU readbacks match corresponding headless renders")
            require(sha(original_executable) == report["binary_sha256"] and sha(args.dmg) == report["dmg_sha256"],
                    "Tested executables changed during verification")
        report["passed"] = True
    except Exception as error:
        report["error"] = f"{type(error).__name__}: {error}"
    (output / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
