#!/usr/bin/env python3
"""Preserve original Mealybug binaries, source, reference PNGs and hardware photos."""
import hashlib
import io
import json
from pathlib import Path
import tarfile
import urllib.request
import zipfile

from fetch_mooneye import immutable_write, relative_member

ROOT = Path(__file__).resolve().parents[1]
REVISION = "70e88fb90b59d19dfbb9c3ac36c64105202bb1f4"
REPOSITORY = "https://github.com/mattcurrie/mealybug-tearoom-tests"
ZIP_URL = f"https://raw.githubusercontent.com/mattcurrie/mealybug-tearoom-tests/{REVISION}/mealybug-tearoom-tests.zip"
SOURCE_URL = f"https://codeload.github.com/mattcurrie/mealybug-tearoom-tests/tar.gz/{REVISION}"
ZIP_SHA = "e2fa6ca96ad48bd64d7297fda042db04c15117a730da3bbd590f139e89b150bd"
SOURCE_SHA = "c7f01ac036fed2bd94f515ac3d1c19924d8b57cf38db0db03ee909eef2712153"
LIMIT = 128 * 1024 * 1024


def archive(path, url, expected):
    if path.exists():
        data = path.read_bytes()
    else:
        with urllib.request.urlopen(url, timeout=60) as response:
            data = response.read(LIMIT + 1)
    if len(data) > LIMIT or hashlib.sha256(data).hexdigest() != expected:
        raise RuntimeError(f"original archive integrity mismatch: {path}")
    immutable_write(path, data)
    return data


def install(files, destination, url, archive_sha):
    entries = {}
    for name, data in files:
        if name in entries:
            raise RuntimeError(f"duplicate archive entry: {name}")
        immutable_write(destination / name, data)
        entries[name] = {"sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)}
    manifest = {"repository": REPOSITORY, "revision": REVISION, "archive_url": url,
                "archive_sha256": archive_sha,
                "oracle_provenance": "author-published emulator reference PNGs; original physical-device photos retained for crosscheck",
                "files": dict(sorted(entries.items()))}
    immutable_write(destination / "manifest.json", (json.dumps(manifest, indent=2) + "\n").encode())
    return manifest


def main():
    roms = ROOT / "roms"
    source = archive(roms / "mealybug-source-original.tar.gz", SOURCE_URL, SOURCE_SHA)
    binary = archive(roms / "mealybug-original.zip", ZIP_URL, ZIP_SHA)
    files = []
    with tarfile.open(fileobj=io.BytesIO(source), mode="r:gz") as package:
        for member in package.getmembers():
            if member.isdir():
                continue
            if not member.isfile() or member.size > LIMIT:
                raise RuntimeError(f"unsupported source member: {member.name}")
            relative = relative_member(member.name, f"mealybug-tearoom-tests-{REVISION}")
            stream = package.extractfile(member)
            if stream is None:
                raise RuntimeError(f"missing member: {member.name}")
            files.append((relative.as_posix(), stream.read()))
    install(files, roms / "mealybug-source", SOURCE_URL, SOURCE_SHA)
    files = []
    with zipfile.ZipFile(io.BytesIO(binary)) as package:
        for member in package.infolist():
            if member.is_dir():
                continue
            relative = relative_member("roms/" + member.filename, "roms")
            if member.file_size > LIMIT or (member.external_attr >> 16) & 0o170000 == 0o120000:
                raise RuntimeError(f"unsupported binary member: {member.filename}")
            files.append((relative.as_posix(), package.read(member)))
    files.append(("LICENSE", (roms / "mealybug-source/LICENSE").read_bytes()))
    manifest = install(files, roms / "mealybug", ZIP_URL, ZIP_SHA)
    print(f"Preserved {sum(name.endswith('.gb') for name in manifest['files'])} original Mealybug ROMs and the author's source/reference/photo archive")


if __name__ == "__main__":
    main()
