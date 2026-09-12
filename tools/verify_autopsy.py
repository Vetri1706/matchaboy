#!/usr/bin/env python3
"""Check native GPU readback against the same live machine's headless HUD."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
import zlib

from verify_all import sha

ROOT = Path(__file__).resolve().parents[1]


def png_rgb(path):
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("invalid capture PNG")
    offset, compressed, geometry = 8, bytearray(), None
    while offset < len(data):
        length = struct.unpack_from(">I", data, offset)[0]
        kind, payload = data[offset+4:offset+8], data[offset+8:offset+8+length]
        crc = struct.unpack_from(">I", data, offset+8+length)[0]
        if zlib.crc32(kind + payload) != crc:
            raise ValueError("capture PNG CRC mismatch")
        offset += length + 12
        if kind == b"IHDR":
            width, height, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", payload)
            if depth != 8 or color not in (2, 6) or compression or filtering or interlace:
                raise ValueError("unsupported capture PNG encoding")
            if not 1 <= width <= 8192 or not 1 <= height <= 8192:
                raise ValueError("invalid capture dimensions")
            geometry = width, height, 3 if color == 2 else 4
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break
    if geometry is None:
        raise ValueError("missing PNG geometry")
    width, height, channels = geometry
    stride = width * channels
    raw = zlib.decompress(compressed)
    if len(raw) != (stride + 1) * height:
        raise ValueError("capture PNG decoded length mismatch")
    result, previous = bytearray(), bytearray(stride)
    for y in range(height):
        predictor = raw[y * (stride + 1)]
        if predictor > 4:
            raise ValueError("invalid PNG row filter")
        row = bytearray(raw[y*(stride+1)+1:(y+1)*(stride+1)])
        if predictor:
            for x in range(stride):
                a, b, c = (row[x-channels] if x >= channels else 0), previous[x], (previous[x-channels] if x >= channels else 0)
                p = a + b - c
                pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
                paeth = a if pa <= pb and pa <= pc else b if pb <= pc else c
                row[x] = (row[x] + (0, a, b, (a+b)//2, paeth)[predictor]) & 255
        if channels == 3:
            result.extend(row)
        else:
            for x in range(width):
                result.extend(row[x*4:x*4+3])
        previous = row
    return width, height, result


def compare(headless, gpu, lcd=None):
    sw, sh, source = png_rgb(headless)
    dw, dh, target = png_rgb(gpu)
    if abs(sw / sh - dw / dh) > 0.001:
        raise ValueError("native window aspect ratio differs from HUD")
    # The HUD uses linear filtering; Windows draws the console LCD separately
    # with nearest filtering to keep game pixels sharp. Model both operations
    # at output pixel centers, including fractional window scaling.
    total = maximum = samples = 0
    lcd_total = lcd_maximum = lcd_samples = 0
    for y in range(0, dh, 4):
        fy = max(0.0, min(sh-1.0, (y+.5)*sh/dh-.5))
        y0 = int(fy); y1 = min(y0+1, sh-1); wy = fy-y0
        for x in range(0, dw, 4):
            fx = max(0.0, min(sw-1.0, (x+.5)*sw/dw-.5))
            x0 = int(fx); x1 = min(x0+1, sw-1); wx = fx-x0
            lcd_pixel = None
            if lcd is not None:
                left, top, width, height, native_width, native_height = lcd
                if left <= fx+.5 < left+width and top <= fy+.5 < top+height:
                    column = int((fx+.5-left)*native_width/width)
                    row = int((fy+.5-top)*native_height/height)
                    # Read each original console pixel from the center of its
                    # nearest-filtered block in the independent headless image.
                    sx = int(left+(column+.5)*width/native_width)
                    sy = int(top+(row+.5)*height/native_height)
                    lcd_pixel = (sy*sw+sx)*3
            for c in range(3):
                upper = source[(y0*sw+x0)*3+c]*(1-wx) + source[(y0*sw+x1)*3+c]*wx
                lower = source[(y1*sw+x0)*3+c]*(1-wx) + source[(y1*sw+x1)*3+c]*wx
                expected = upper*(1-wy) + lower*wy
                if lcd_pixel is not None:
                    expected = source[lcd_pixel+c]
                error = abs(target[(y*dw+x)*3+c] - expected)
                total += error; maximum = max(maximum, error); samples += 1
                if lcd_pixel is not None:
                    lcd_total += error; lcd_maximum = max(lcd_maximum, error); lcd_samples += 1
    return {"samples": samples, "mean_channel_error": total/samples,
            "maximum_channel_error": maximum, "source_dimensions": [sw, sh],
            "gpu_dimensions": [dw, dh],
            "lcd_samples": lcd_samples, "lcd_mean_channel_error": lcd_total/lcd_samples if lcd_samples else None,
            "lcd_maximum_channel_error": lcd_maximum if lcd_samples else None,
            "passed": total/samples <= 2.0 and (lcd is None or (lcd_samples > 0 and lcd_maximum <= 1))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--binary", type=Path, default=ROOT/"build/autopsy")
    parser.add_argument("--rom", type=Path, default=ROOT/"roms/acid2/dmg-acid2.gb")
    parser.add_argument("--player", action="store_true", help="verify the Windows player display instead of Inspector")
    args = parser.parse_args()
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=False)
    binary, rom = args.binary.resolve(), args.rom.resolve()
    gba = rom.suffix.lower() == ".gba"
    summary = {"passed": False, "binary_sha256": sha(binary), "rom_sha256": sha(rom), "commands": []}
    try:
        for name, option in (("headless", "--headless"), ("gpu", "--window-test")):
            command = [str(binary), str(rom), option, "--frames", "120",
                       "--capture", str(output/(name+".png"))]
            # Positioning switches deliberately open Inspector in native
            # players. Omit them when testing the actual game presentation.
            if not gba and not args.player: command.extend(["--line", "48", "--dot", "115"])
            if sys.platform in ("win32", "darwin") and not args.player:
                command.append("--inspector")
            with (output/(name+".log")).open("w") as log:
                result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=45, check=False)
            summary["commands"].append({"command": command, "exit_code": result.returncode})
            if result.returncode:
                detail = (output/(name+".log")).read_text(errors="replace")[-4000:].strip()
                raise RuntimeError(f"{name} capture failed (exit {result.returncode}): {detail}")
        cpu_state, gpu_state = [json.loads((output/(name+".png.json")).read_text()) for name in ("headless", "gpu")]
        if not gpu_state["gpu_readback"] or cpu_state["gpu_readback"]:
            raise RuntimeError("native capture was not read back from GPU")
        for key in (("frames", "pc", "cpsr", "dispcnt", "inspector_view") if gba else ("frames", "cycles", "ly", "dot", "mode", "fifo_depth")):
            if cpu_state[key] != gpu_state[key]:
                raise RuntimeError(f"capture machine states differ: {key}")
        if args.player and (cpu_state.get("inspector_view") is not False or gpu_state.get("inspector_view") is not False):
            raise RuntimeError("player capture unexpectedly opened Inspector")
        if not gba and not args.player and (cpu_state["mode"] != 3 or cpu_state["fifo_depth"] == 0):
            raise RuntimeError("capture missed active mode-three FIFO")
        summary["machine"] = cpu_state
        lcd = None
        if sys.platform == "win32":
            native_width, native_height = (240, 160) if gba else (160, 144)
            if args.player:
                height = 533 if gba else 720
                left = 48 if cpu_state["controls_visible"] else 240
                lcd = (left, 136+(720-height)//2, 800, height, native_width, native_height)
            else:
                lcd = (48, 232, 640, 427 if gba else 576, native_width, native_height)
        summary["pixel_comparison"] = compare(output/"headless.png", output/"gpu.png", lcd)
        summary["binary_unchanged"] = sha(binary) == summary["binary_sha256"]
        summary["rom_unchanged"] = sha(rom) == summary["rom_sha256"]
        summary["passed"] = summary["pixel_comparison"]["passed"] and summary["binary_unchanged"] and summary["rom_unchanged"]
    except Exception as error:
        summary["error"] = str(error)
    (output/"summary.json").write_text(json.dumps(summary, indent=2)+"\n")
    print(json.dumps(summary, indent=2))
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
