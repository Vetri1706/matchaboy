#!/usr/bin/env python3
"""Verify a real Mac bundle and prove its resource/Info.plist seals reject edits.

This checks integrity, not Developer ID trust or Apple notarization. It never
changes Gatekeeper settings, removes quarantine, or modifies the supplied app.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def signature_result(app):
    return subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict",
                           "--verbose=2", str(app)], capture_output=True, text=True)


def verify_bundle(app):
    if sys.platform != "darwin":
        raise RuntimeError("bundle signature verification requires macOS")
    result = signature_result(app)
    if result.returncode:
        raise RuntimeError("Invalid Mac app signature:\n" + result.stderr)
    if not (app / "Contents/_CodeSignature/CodeResources").is_file():
        raise RuntimeError("Mac app has no sealed resource manifest")


def verify_tamper_detection(app):
    verify_bundle(app)
    # Work only on a disposable copy; never execute either tampered bundle.
    with tempfile.TemporaryDirectory(prefix="Matchaboy signature check ") as temporary:
        copied = Path(temporary) / app.name
        shutil.copytree(app, copied)
        verify_bundle(copied)
        for relative in ("Contents/Resources/Arcade/matcha-garden.gb", "Contents/Info.plist"):
            target = copied / relative
            original = target.read_bytes()
            target.write_bytes(original + b"\n")
            if signature_result(copied).returncode == 0:
                raise RuntimeError("Signature accepted a modified file: " + relative)
            target.write_bytes(original)
            verify_bundle(copied)
    return {"integrity_verified": True, "modified_resource_rejected": True,
            "modified_info_plist_rejected": True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    report = verify_tamper_detection(args.app.resolve())
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n")
    print("PASS Mac bundle integrity; altered ROM and Info.plist rejected")


if __name__ == "__main__":
    main()
