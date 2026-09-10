#!/usr/bin/env python3
"""Fetch original external test binaries; never synthesize a replacement oracle."""
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
REVISION = "c240dd7d700e5c0b00a7bbba52b53e4ee67b5f15"
URL = f"https://codeload.github.com/retrio/gb-test-roms/tar.gz/{REVISION}"


def main():
    request = urllib.request.Request(URL, headers={"User-Agent": "DMG-headless-verifier/1"})
    with urllib.request.urlopen(request, timeout=60) as response:
        data = response.read(32 * 1024 * 1024 + 1)
    if len(data) > 32 * 1024 * 1024:
        raise RuntimeError("test archive exceeds size limit")
    destination = ROOT / "roms" / "blargg"
    manifest = {"repository": "https://github.com/retrio/gb-test-roms", "revision": REVISION,
                "archive_url": URL, "archive_sha256": hashlib.sha256(data).hexdigest(), "files": {}}
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
        for member in archive:
            parts = PurePosixPath(member.name).parts
            if len(parts) < 2 or ".." in parts or not member.isfile():
                continue
            relative = PurePosixPath(*parts[1:])
            if relative.suffix not in (".gb", ".s", ".inc", ".txt", ".asm"):
                continue
            if member.size > 4 * 1024 * 1024:
                raise RuntimeError("oversized archive member")
            source = archive.extractfile(member)
            if source is None:
                raise RuntimeError("missing archive member")
            contents = source.read()
            name = relative.as_posix()
            path = destination / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(contents)
            manifest["files"][name] = {"bytes": len(contents), "sha256": hashlib.sha256(contents).hexdigest()}
    (ROOT / "roms" / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Fetched {len(manifest['files'])} original binary/source files at {REVISION}")


if __name__ == "__main__":
    main()
