#!/usr/bin/env python3
"""Exact framebuffer comparison against an independent published test oracle."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import time
import zlib

ROOT = Path(__file__).resolve().parents[1]


def write_png(path, pixels):
    def chunk(kind, payload):
        return struct.pack(">I", len(payload))+kind+payload+struct.pack(">I",zlib.crc32(kind+payload))
    rows=b"".join(b"\x00"+pixels[y*160:(y+1)*160] for y in range(144))
    path.write_bytes(b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",160,144,8,0,0,0,0))+
                     chunk(b"IDAT",zlib.compress(rows))+chunk(b"IEND",b""))


def png_gray(data):
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("bad PNG signature")
    position = 8
    compressed = bytearray()
    palette = b""
    while position < len(data):
        length = struct.unpack_from(">I", data, position)[0]
        kind = data[position+4:position+8]
        payload = data[position+8:position+8+length]
        crc = struct.unpack_from(">I",data,position+8+length)[0]
        if zlib.crc32(kind+payload) != crc:
            raise ValueError("PNG chunk CRC mismatch")
        position += length+12
        if kind == b"IHDR":
            width,height,depth,color,compression,filter_method,interlace = struct.unpack(">IIBBBBB",payload)
            if (width,height)!=(160,144) or compression or filter_method or interlace:
                raise ValueError("unsupported reference PNG geometry or coding")
        elif kind == b"PLTE":
            palette = payload
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break
    channels = {0:1,2:3,3:1,4:2,6:4}[color]
    if depth not in (1,2,4,8) or (channels>1 and depth!=8):
        raise ValueError("unsupported PNG depth")
    stride = (width*channels*depth+7)//8
    bpp = max(1,(channels*depth+7)//8)
    raw = zlib.decompress(compressed)
    if len(raw)!=(stride+1)*height:
        raise ValueError("PNG decoded size mismatch")
    previous = bytearray(stride)
    pixels = bytearray()
    for y in range(height):
        filter_type = raw[y*(stride+1)]
        row = bytearray(raw[y*(stride+1)+1:(y+1)*(stride+1)])
        for x in range(stride):
            a = row[x-bpp] if x>=bpp else 0
            b = previous[x]
            c = previous[x-bpp] if x>=bpp else 0
            p = a+b-c
            pa,pb,pc = abs(p-a),abs(p-b),abs(p-c)
            paeth = a if pa<=pb and pa<=pc else b if pb<=pc else c
            predictors = (0,a,b,(a+b)//2,paeth)
            row[x]=(row[x]+predictors[filter_type])&255
        for x in range(width):
            if color in (0,3):
                value = (row[x*depth//8] >> (8-depth-(x*depth%8)))&((1<<depth)-1)
                if color==3:
                    r,g,b=palette[value*3:value*3+3]
                    if r!=g or g!=b:
                        raise ValueError("reference is not grayscale")
                    pixels.append(r)
                else:
                    pixels.append(value*255//((1<<depth)-1))
            else:
                values=row[x*channels:(x+1)*channels]
                if color in (2,6) and not values[0]==values[1]==values[2]:
                    raise ValueError("reference is not grayscale")
                pixels.append(values[0])
        previous=row
    return bytes(pixels)


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--output",type=Path)
    parser.add_argument("--binary",type=Path,default=ROOT/"build"/"dmg")
    args=parser.parse_args()
    binary=args.binary.resolve()
    binary_before=hashlib.sha256(binary.read_bytes()).hexdigest()
    source_paths=sorted((ROOT/"src").glob("*.cpp"))+sorted((ROOT/"include"/"dmg").glob("*.hpp"))
    source_before={path.relative_to(ROOT).as_posix():hashlib.sha256(path.read_bytes()).hexdigest()
                   for path in source_paths}
    output=args.output or ROOT/"artifacts"/f"ppu-{time.time_ns()}"
    output.mkdir(parents=True,exist_ok=False)
    external=ROOT/"roms"/"acid2"
    manifest=json.loads((external/"manifest.json").read_text())
    for name,item in manifest["files"].items():
        if hashlib.sha256((external/name).read_bytes()).hexdigest()!=item["sha256"]:
            raise RuntimeError("external PPU oracle integrity mismatch")
    expected=png_gray((external/"reference-dmg.png").read_bytes())
    with (output/"stdout.txt").open("w") as stdout,(output/"stderr.txt").open("w") as stderr:
        result=subprocess.run([str(binary),str(external/"dmg-acid2.gb"),"--frames","120",
                               "--report",str(output/"run.json"),"--frame-out",str(output/"actual.pgm"),
                               "--trace-out",str(output/"trace.txt")],stdout=stdout,stderr=stderr,timeout=195,check=False)
    run_report=json.loads((output/"run.json").read_text())
    frame_run_completed=(run_report.get("status")=="completed" and run_report.get("frames")==120)
    actual=(output/"actual.pgm").read_bytes().split(b"\n",3)[3]
    if len(actual)!=len(expected):
        raise RuntimeError("framebuffer size mismatch")
    mismatch=[{"x":i%160,"y":i//160,"expected":e,"actual":a} for i,(e,a) in enumerate(zip(expected,actual)) if e!=a]
    binary_after=hashlib.sha256(binary.read_bytes()).hexdigest()
    source_after={path.relative_to(ROOT).as_posix():hashlib.sha256(path.read_bytes()).hexdigest()
                  for path in source_paths}
    oracle_unchanged=all(hashlib.sha256((external/name).read_bytes()).hexdigest()==item["sha256"]
                         for name,item in manifest["files"].items())
    integrity=binary_before==binary_after and source_before==source_after and oracle_unchanged
    summary={"passed":result.returncode==0 and frame_run_completed and not mismatch and integrity,"mismatching_pixels":len(mismatch),
             "total_pixels":23040,"rom":manifest,"first_mismatches":mismatch[:100],
             "run":run_report,"exit_code":result.returncode,
             "binary":str(binary),"binary_sha256":binary_before,"source_sha256":source_before,
             "integrity":{"binary_unchanged":binary_before==binary_after,
                          "source_unchanged":source_before==source_after,"oracle_unchanged":oracle_unchanged},
             "scope":"PPU rendering/priority acceptance; acid2 does not certify precise mode-3 timing"}
    temporary=output/"summary.json.tmp"
    temporary.write_text(json.dumps(summary,indent=2)+"\n")
    temporary.replace(output/"summary.json")
    (output/"expected.pgm").write_bytes(b"P5\n160 144\n255\n"+expected)
    write_png(output/"actual.png",actual)
    write_png(output/"expected.png",expected)
    print(f"{'PASS' if summary['passed'] else 'FAIL'} dmg-acid2: {len(mismatch)} / 23040 pixels differ; {output}")
    return 0 if summary["passed"] else 1


if __name__=="__main__":
    sys.exit(main())
