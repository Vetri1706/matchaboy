#!/usr/bin/env python3
"""Run actual two-process Link-ROM peers through a bounded real UDP fault relay."""
import argparse
import hashlib
import heapq
import json
from pathlib import Path
import random
import selectors
import socket
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def link_rom(side, scenario="feedback"):
    """Original executable SM83 fixture: VBlank, P1 input, SB/SC and received-byte WRAM log."""
    rom = bytearray(32768)
    rom[0x100:0x103] = bytes([0xC3, 0x50, 0x01])
    rom[0x40] = 0xD9  # real VBlank interrupt handler: RETI
    rom[0x134:0x13F] = b"UDP-LINK-V1"
    code = bytearray([0xF3, 0x31, 0xFE, 0xFF, 0xAF, 0xE0, 0x0F, 0x3E, 1, 0xEA, 0xFF, 0xFF,
                      0x21, 0x00, 0xC1, 0x06, 0, 0x0E, 0, 0xFB])
    loop = 0x150 + len(code)
    code += bytes([0x76, 0x04, 0x3E, 0x10, 0xE0, 0, 0xF0, 0, 0xA8, 0xA9, 0xE0, 1])
    if side == 0:
        code += bytes([0] * 8)  # Give the external-clock console time to arm SC.
    code += bytes([0x3E, 0x81 if side == 0 else 0x80, 0xE0, 2])
    poll = 0x150 + len(code)
    code += bytes([0xF0, 2, 0xE6, 0x80, 0x20, 0xFA, 0xF0, 1, 0x4F, 0x22,
                   0xC3, loop & 255, loop >> 8])
    assert poll + 6 == 0x150 + len(code) - 7
    if scenario == "idle-peer" and side == 1:
        code[-3:] = bytes([0x76, 0x18, 0xFD])  # HALT/JR: do not rearm serial after first exchange.
    rom[0x150:0x150 + len(code)] = code
    check = 0
    for value in rom[0x134:0x14D]:
        check = (check - value - 1) & 255
    rom[0x14D] = check
    return rom


def unused_ports(count):
    sockets = []
    for _ in range(count):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.bind(("127.0.0.1", 0))
        sockets.append(s)
    ports = [s.getsockname()[1] for s in sockets]
    for s in sockets:
        s.close()
    return ports


def campaign(binary, roms, output, frames, delay_ms, loss, seed, pace_ms, scenario):
    output.mkdir()
    ports = unused_ports(4)
    rng = random.Random(seed)
    sel = selectors.DefaultSelector()
    relays = []
    for side in range(2):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.bind(("127.0.0.1", ports[2 + side]))
        s.setblocking(False)
        sel.register(s, selectors.EVENT_READ, side)
        relays.append(s)
    processes, streams = [], []
    statistics = dict(received=0, dropped=0, forwarded=0, reordered=0, peak_pending=0,
                      base_one_way_delay_ms=delay_ms, jitter_ms=(20 if delay_ms else 0),
                      maximum_one_way_delay_ms=(delay_ms + 20 if delay_ms else 0),
                      expected_round_trip_ms=2 * delay_ms, configured_loss=loss,
                      relay="real recvfrom followed by delayed/dropped sendto; both directions")
    pending = []
    serial = 0
    latest_forwarded = [-1, -1]
    begun = time.monotonic()
    timeout = max(30, frames * (pace_ms / 1000 + 0.3) + 60)
    try:
        for side in range(2):
            directory = output / f"peer{side}"
            directory.mkdir()
            stdout = (directory / "stdout.txt").open("w")
            stderr = (directory / "stderr.txt").open("w")
            streams.extend([stdout, stderr])
            command = [str(binary), "--rom", str(roms[side]), "--peer-rom", str(roms[1 - side]),
                       "--side", str(side), "--port", str(ports[side]), "--peer-port", str(ports[2 + side]),
                       "--session", str(seed + 100), "--frames", str(frames), "--seed", str(17 + side * 100),
                       "--pace-ms", str(pace_ms), "--timeout", str(int(timeout)), "--output", str(directory)]
            (directory / "command.json").write_text(json.dumps(command, indent=2) + "\n")
            processes.append(subprocess.Popen(command, stdout=stdout, stderr=stderr))
        while any(p.poll() is None for p in processes):
            if any(p.poll() not in (None, 0) for p in processes):
                raise RuntimeError(f"peer failed: {output}; inspect peer stderr")
            now = time.monotonic()
            if now - begun > timeout + 3:
                raise RuntimeError("peer subprocess timeout")
            for key, _ in sel.select(0.001):
                while True:
                    try:
                        data, sender = key.fileobj.recvfrom(65535)
                    except BlockingIOError:
                        break
                    side = key.data
                    if sender != ("127.0.0.1", ports[side]):
                        raise RuntimeError("unexpected sender at UDP relay")
                    statistics["received"] += 1
                    serial += 1
                    if rng.random() < loss:
                        statistics["dropped"] += 1
                        continue
                    if len(pending) >= 65536:
                        raise RuntimeError("UDP relay queue capacity exceeded")
                    heapq.heappush(pending, (time.monotonic() + (max(0, delay_ms + rng.uniform(-20, 20)) if delay_ms else 0) / 1000,
                                             serial, side, data))
                    statistics["peak_pending"] = max(statistics["peak_pending"], len(pending))
            now = time.monotonic()
            while pending and pending[0][0] <= now:
                _, sequence, side, data = heapq.heappop(pending)
                if sequence < latest_forwarded[side]:
                    statistics["reordered"] += 1
                latest_forwarded[side] = max(sequence, latest_forwarded[side])
                relays[1 - side].sendto(data, ("127.0.0.1", ports[1 - side]))
                statistics["forwarded"] += 1
        for p in processes:
            if p.returncode:
                raise RuntimeError(f"peer failed: {output}; inspect peer stderr")
    finally:
        for p in processes:
            if p.poll() is None:
                p.terminate()
            p.wait()
        for stream in streams:
            stream.close()
        for s in relays:
            s.close()
        sel.close()
        statistics["seconds"] = time.monotonic() - begun
        (output / "relay.json").write_text(json.dumps(statistics, indent=2) + "\n")
    sent = [(output / f"peer{s}" / "outgoing.bin").read_bytes() for s in range(2)]
    memory = [(output / f"peer{s}" / "memory.bin").read_bytes() for s in range(2)]
    expected = [bytearray(), bytearray()]
    previous = [0, 0]
    mask = (1 << 64) - 1
    for frame in range(frames):
        values = []
        for side in range(2):
            x = (frame + (17 + side * 100) * 0x9e3779b97f4a7c15) & mask
            x = ((x ^ (x >> 30)) * 0xbf58476d1ce4e5b9) & mask
            x = ((x ^ (x >> 27)) * 0x94d049bb133111eb) & mask
            buttons = (x ^ (x >> 31)) & 255
            value = (0xD0 | (15 ^ (buttons >> 4))) ^ ((frame + 1) & 255) ^ previous[side]
            values.append(value)
            expected[side].append(value)
        previous = [values[1] if scenario != "idle-peer" or frame == 0 else 255, values[0]]
    for side in range(2):
        if scenario == "idle-peer" and side == 1:
            expected[side] = expected[side][:1]
        if sent[side] != expected[side]:
            raise RuntimeError(f"console {side}: independent outgoing-byte recurrence mismatch")
        received = (sent[1] + bytes([255]) * (frames - 1)) if scenario == "idle-peer" and side == 0 else sent[1 - side]
        if scenario == "idle-peer" and side == 1:
            received = received[:1]
        if memory[side][0xC100:0xC100 + len(received)] != received:
            raise RuntimeError(f"console {side}: WRAM received bytes differ from peer SB output")
    statistics["independent_serial_oracle"] = True
    return statistics


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/netplay")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--frames", type=int, default=1000)
    parser.add_argument("--pace-ms", type=int, default=17)
    parser.add_argument("--delay-ms", type=int, default=100)
    parser.add_argument("--loss", type=float, default=0.05)
    parser.add_argument("--seed", type=int, default=314159)
    parser.add_argument("--only-clean", action="store_true")
    parser.add_argument("--scenario", choices=["feedback", "idle-peer"], default="feedback")
    args = parser.parse_args()
    if args.frames < 1 or args.delay_ms < 0 or not 0 <= args.loss <= .5:
        parser.error("invalid frame/delay/loss configuration")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    binary = args.binary.resolve()
    binary_hash = digest(binary)
    roms = [output / f"link{side}.gb" for side in range(2)]
    for side, path in enumerate(roms):
        path.write_bytes(link_rom(side, args.scenario))
    summary = {"passed": False, "binary": str(binary), "binary_sha256": binary_hash,
               "oracle": "original homebrew two-console serial program; not commercial-ROM validation",
               "scenario": args.scenario, "frames_per_console": args.frames, "rom_sha256": [digest(p) for p in roms]}
    try:
        summary["clean"] = campaign(binary, roms, output / "clean", args.frames, 0, 0, args.seed, args.pace_ms, args.scenario)
        if not args.only_clean:
            summary["chaos"] = campaign(binary, roms, output / "chaos", args.frames, args.delay_ms,
                                         args.loss, args.seed + 1, args.pace_ms, args.scenario)
            for side in range(2):
                for name in ("frames.txt", "final.snapshot", "confirmed.video", "confirmed.pcm"):
                    clean, chaos = [output / mode / f"peer{side}" / name for mode in ("clean", "chaos")]
                    if clean.read_bytes() != chaos.read_bytes():
                        raise RuntimeError(f"console {side}: clean/chaos {name} differs")
            if args.loss and summary["chaos"]["dropped"] == 0:
                raise RuntimeError("no actual datagrams dropped")
        summary["binary_unchanged"] = digest(binary) == binary_hash
        summary["passed"] = summary["binary_unchanged"]
    except Exception as exc:
        summary["error"] = str(exc)
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
