#!/usr/bin/env python3
"""Black-box protocol rejection tests: native CPU peer and real UDP adversarial client."""
import hashlib
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest
import zlib
from verify_netplay import ROOT, link_rom, unused_ports


def wire(kind, sequence, payload=b"", frame=0, cycle=0, ack=1):
    raw = bytearray(struct.pack(">IHBBQIIQQQHHI", 0x444E504C, 1, kind, 0, 777, sequence, ack, 0,
                                frame, cycle, len(payload), 0, 0) + payload)
    struct.pack_into(">I", raw, 52, zlib.crc32(raw))
    return raw


def bundle(revision=1, dependency=0, count=0, part=0, chunks=1, end=70224, events=b""):
    return struct.pack(">IIHBBBBB BQQQIII", revision, dependency, count, part, chunks,
                       0, 0, 0, 0, end, 0, 0, 0, 0, 0) + events


class ProtocolTests(unittest.TestCase):
    def probe(self, payloads, expected, stopped=False):
        (ROOT / "artifacts").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="netplay-protocol-", dir=ROOT / "artifacts") as directory:
            out = Path(directory)
            rom = out / "link.gb"
            program = link_rom(0)
            if stopped:
                program[0x150:0x158] = bytes([0xF3, 0x3E, 0x30, 0xE0, 0, 0x10, 0, 0])
            rom.write_bytes(program)
            port, remote = unused_ports(2)
            peer = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            peer.bind(("127.0.0.1", remote))
            peer.settimeout(2)
            binary = os.environ.get("MATCHA_NETPLAY", str(ROOT / "build" / ("netplay.exe" if sys.platform == "win32" else "netplay")))
            command = [binary, "--rom", str(rom), "--peer-rom", str(rom),
                       "--port", str(port), "--peer-port", str(remote), "--side", "0", "--frames", "4",
                       "--session", "777", "--pace-ms", "1", "--timeout", "3", "--output", str(out / "peer")]
            process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                packet, address = peer.recvfrom(2048)
                self.assertEqual(address[1], port)
                self.assertEqual(packet[6], 1)
                hello = bytes([1]) + struct.pack(">QQ", 4, 117) + hashlib.sha256(rom.read_bytes()).digest()
                peer.sendto(wire(1, 1, hello), address)
                peer.sendto(wire(6, 0), address)
                for packet in payloads:
                    peer.sendto(packet, address)
                _, stderr = process.communicate(timeout=5)
                self.assertNotEqual(process.returncode, 0)
                self.assertIn(expected, stderr)
                self.assertFalse((out / "peer/report.json").exists())
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()
                peer.close()

    def test_future_frame_outside_ring(self):
        self.probe([wire(2, 2, bundle(), frame=60)], "remote frame exceeds fixed history window")

    def test_oversized_event_count(self):
        self.probe([wire(2, 2, bundle(count=257))], "invalid frame bundle metadata")

    def test_fragment_revision_inconsistent(self):
        events = b"".join(struct.pack(">QQBBBB", i + 1, 0, 0, 0, 0x80, 0) for i in range(58))
        self.probe([wire(2, 2, bundle(count=58, chunks=2, events=events[:57 * 20])),
                    wire(2, 3, bundle(count=58, part=1, chunks=2, end=70228, events=events[57 * 20:]))],
                   "inconsistent frame revision fragments")

    def test_invalid_finish(self):
        self.probe([wire(5, 2, frame=3, cycle=3 * 70224)], "invalid finish packet")

    def test_same_revision_cannot_change_state(self):
        self.probe([wire(2, 2, bundle()), wire(2, 3, bundle(dependency=1, end=70228))],
                   "frame revision changed immutable state")

    def test_stop_rom_does_not_hang(self):
        self.probe([], "oscillator stopped at cycle", stopped=True)

    def test_invalid_event_clock(self):
        event = struct.pack(">QQBBBB", 80000, 0, 1, 0, 0x81, 1)
        self.probe([wire(2, 2, bundle(count=1, events=event))], "invalid serial event in frame revision")


if __name__ == "__main__":
    unittest.main()
