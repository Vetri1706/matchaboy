#!/usr/bin/env python3
"""Fetch byte-for-byte official Mooneye ROMs, symbols and original sources."""
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import tarfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
REVISION = "31510e12eea6286d36eea060a6adde755e1067aa"
RELEASE = "mts-20260714-0944-31510e1"
ROM_URL = f"https://gekkio.fi/files/mooneye-test-suite/{RELEASE}/{RELEASE}.zip"
ROM_SHA256 = "18aa29462dfe1fcd32a2cb3621733abdc72410074918b9245aa99a6920f7d3f2"
SOURCE_URL = f"https://codeload.github.com/Gekkio/mooneye-test-suite/tar.gz/{REVISION}"
SOURCE_SHA256 = "07a253c65320abdefaf0da4290562c7318309d1945e3a2fb282292660e635674"
MAX_ARCHIVE_BYTES = 16 * 1024 * 1024


def digest(data):
    return hashlib.sha256(data).hexdigest()


def immutable_write(path, data):
    if path.is_symlink():
        raise RuntimeError(f"refusing symlink: {path}")
    if path.exists():
        if path.read_bytes() != data:
            raise RuntimeError(f"existing external artifact differs: {path}")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as output:
        output.write(data)


def archive(path, url, expected):
    if path.exists():
        data = path.read_bytes()
    else:
        request = urllib.request.Request(url, headers={"User-Agent": "DMG-headless-verifier/1"})
        with urllib.request.urlopen(request, timeout=60) as response:
            data = response.read(MAX_ARCHIVE_BYTES + 1)
    if len(data) > MAX_ARCHIVE_BYTES or digest(data) != expected:
        raise RuntimeError(f"official archive checksum mismatch: {path}")
    immutable_write(path, data)
    return data


def relative_member(name, prefix):
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts or "\\" in name or not path.parts:
        raise RuntimeError(f"unsafe archive member: {name}")
    if path.parts[0] != prefix or len(path.parts) < 2:
        raise RuntimeError(f"unexpected archive root: {name}")
    return Path(*path.parts[1:])


def install(files, destination, archive_url, archive_sha):
    entries = {}
    for relative, data in files:
        name = relative.as_posix()
        if name in entries:
            raise RuntimeError(f"duplicate archive member: {name}")
        immutable_write(destination / relative, data)
        entries[name] = {"sha256": digest(data), "bytes": len(data)}
    manifest = {"repository": "https://github.com/Gekkio/mooneye-test-suite",
                "revision": REVISION, "release": RELEASE, "archive_url": archive_url,
                "archive_sha256": archive_sha, "files": dict(sorted(entries.items()))}
    immutable_write(destination / "manifest.json", (json.dumps(manifest, indent=2) + "\n").encode())
    return manifest


def main():
    roms = ROOT / "roms"
    binary_data = archive(roms / "mooneye-original.zip", ROM_URL, ROM_SHA256)
    source_data = archive(roms / "mooneye-source-original.tar.gz", SOURCE_URL, SOURCE_SHA256)
    with zipfile.ZipFile(io.BytesIO(binary_data)) as package:
        binary_files = []
        for member in package.infolist():
            if member.is_dir():
                continue
            if member.file_size > MAX_ARCHIVE_BYTES or (member.external_attr >> 16) & 0o170000 == 0o120000:
                raise RuntimeError(f"unsafe ZIP entry: {member.filename}")
            binary_files.append((relative_member(member.filename, RELEASE), package.read(member)))
    with tarfile.open(fileobj=io.BytesIO(source_data), mode="r:gz") as package:
        source_files = []
        for member in package.getmembers():
            if member.isdir():
                continue
            if not member.isfile() or member.size > MAX_ARCHIVE_BYTES:
                raise RuntimeError(f"unsafe source entry: {member.name}")
            stream = package.extractfile(member)
            if stream is None:
                raise RuntimeError(f"missing source entry: {member.name}")
            source_files.append((relative_member(member.name, f"mooneye-test-suite-{REVISION}"), stream.read()))
    sources = install(source_files, roms / "mooneye-source", SOURCE_URL, SOURCE_SHA256)
    # The author's binary archive does not necessarily bundle the MIT license.
    # Preserve its exact source bytes beside the ROMs as an attributed addition.
    if not any(relative.as_posix() == "LICENSE" for relative, _ in binary_files):
        binary_files.append((Path("LICENSE"), (roms / "mooneye-source" / "LICENSE").read_bytes()))
    binaries = install(binary_files, roms / "mooneye", ROM_URL, ROM_SHA256)
    count = sum(name.endswith(".gb") for name in binaries["files"])
    print(f"Verified {count} original Mooneye ROMs and {len(sources['files'])} source files at {REVISION}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
