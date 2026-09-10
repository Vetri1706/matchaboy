#!/usr/bin/env python3
"""Download dmg-acid2 v1.0 and its author's DMG reference frame."""
import hashlib
import json
from pathlib import Path
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
REV = "dc2295408f881637ff69f784d8d93f3d2db30181"
FILES = {
    "dmg-acid2.gb": "https://github.com/mattcurrie/dmg-acid2/releases/download/v1.0/dmg-acid2.gb",
    "reference-dmg.png": f"https://raw.githubusercontent.com/mattcurrie/dmg-acid2/{REV}/img/reference-dmg.png",
    "LICENSE": f"https://raw.githubusercontent.com/mattcurrie/dmg-acid2/{REV}/LICENSE",
    "README.md": f"https://raw.githubusercontent.com/mattcurrie/dmg-acid2/{REV}/README.md",
}
destination = ROOT / "roms" / "acid2"
destination.mkdir(parents=True, exist_ok=True)
manifest = {"repository": "https://github.com/mattcurrie/dmg-acid2", "release": "v1.0", "revision": REV, "files": {}}
for name,url in FILES.items():
    request = urllib.request.Request(url, headers={"User-Agent": "DMG-headless-verifier/1"})
    with urllib.request.urlopen(request, timeout=60) as response:
        contents = response.read(4*1024*1024+1)
    if len(contents)>4*1024*1024:
        raise RuntimeError("external artifact too large")
    (destination / name).write_bytes(contents)
    manifest["files"][name] = {"url": url, "sha256": hashlib.sha256(contents).hexdigest(), "bytes": len(contents)}
(destination / "manifest.json").write_text(json.dumps(manifest, indent=2)+"\n")
print("Fetched original dmg-acid2 v1.0 ROM and reference frame")
