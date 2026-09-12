#!/usr/bin/env python3
"""Drive two real FIFA FriendSession processes over UDP, with an optional relay.

Requires a user-supplied FIFA 07 GBA cartridge and its optional Matchaboy save.
No cartridge bytes or saved progress are included in this repository.
"""
from __future__ import annotations

import argparse
import hashlib
import heapq
import json
import pathlib
import queue
import random
import secrets
import select
import shutil
import socket
import subprocess
import tempfile
import threading
import time


def sha(path: pathlib.Path) -> str | None:
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.exists() else None


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


class Relay:
    """Forward real datagrams; randomness affects delivery, never emulator state."""

    def __init__(self, host_port: int) -> None:
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(("127.0.0.1", 0))
        self.socket.setblocking(False)
        self.port = int(self.socket.getsockname()[1])
        self.host = ("127.0.0.1", host_port)
        self.join: tuple[str, int] | None = None
        self.pending: list[tuple[float, int, bytes, tuple[str, int]]] = []
        self.rng = random.Random(0xF1FA07)
        self.received = self.dropped = self.forwarded = self.maximum_queue = 0

    def service(self) -> None:
        while True:
            try:
                packet, source = self.socket.recvfrom(65536)
            except BlockingIOError:
                break
            self.received += 1
            if source == self.host:
                destination = self.join
            elif self.join is None or source == self.join:
                self.join = source
                destination = self.host
            else:
                continue
            if destination is None or self.rng.random() < 0.05:
                self.dropped += 1
                continue
            delivery = time.monotonic() + self.rng.uniform(0.030, 0.070)
            heapq.heappush(self.pending, (delivery, self.received, packet, destination))
            self.maximum_queue = max(self.maximum_queue, len(self.pending))
        now = time.monotonic()
        while self.pending and self.pending[0][0] <= now:
            _, _, packet, destination = heapq.heappop(self.pending)
            self.socket.sendto(packet, destination)
            self.forwarded += 1

    def close(self) -> None:
        self.socket.close()


def reader(process: subprocess.Popen[str], role: str, events: queue.Queue, log: pathlib.Path) -> None:
    assert process.stdout is not None
    with log.open("w", encoding="utf-8") as output:
        for line in process.stdout:
            output.write(line)
            output.flush()
            try:
                events.put((role, json.loads(line)))
            except json.JSONDecodeError:
                events.put((role, {"event": "diagnostic", "message": line.rstrip()}))


def run_phase(args: argparse.Namespace, fault: bool) -> dict:
    name = "jitter-loss" if fault else "loopback"
    output = args.output / name
    output.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    processes: dict[str, subprocess.Popen[str]] = {}
    threads = []
    events: queue.Queue = queue.Queue()
    verified: dict[str, dict] = {}
    checkpoints: dict[str, dict[int, dict]] = {"host": {}, "join": {}}
    relay: Relay | None = None
    with tempfile.TemporaryDirectory(prefix="matchaboy-fifa-network-") as temporary:
        work = pathlib.Path(temporary)
        stop = work / "stop"
        host_port = free_port()
        if fault:
            relay = Relay(host_port)
        code = secrets.token_hex(16)
        try:
            for role in ("host", "join"):
                console = work / role
                console.mkdir()
                rom = console / args.rom.name
                shutil.copyfile(args.rom, rom)
                save = args.rom.with_suffix(".matchaboy.sav")
                if save.exists():
                    shutil.copyfile(save, rom.with_suffix(".matchaboy.sav"))
                destination = host_port if role == "host" or relay is None else relay.port
                command = [str(args.binary), str(rom), str(output / role), role, "127.0.0.1",
                           str(destination), code, str(stop), "paced" if fault else "unpaced"]
                processes[role] = subprocess.Popen(command, stdout=subprocess.PIPE,
                                                   stderr=subprocess.STDOUT, text=True, bufsize=1)
                thread = threading.Thread(target=reader,
                                          args=(processes[role], role, events, output / f"{role}.log"))
                thread.start()
                threads.append(thread)
            last_progress = time.monotonic()
            while len(verified) != 2:
                if time.monotonic() - started > args.timeout:
                    raise RuntimeError(f"{name}: timed out waiting for verified frames")
                if relay:
                    relay.service()
                while True:
                    try:
                        role, event = events.get_nowait()
                    except queue.Empty:
                        break
                    kind = event.get("event")
                    if kind == "verified":
                        if event["frame"] != 5149 or event["verified_frame"] < 5100:
                            raise RuntimeError("Probe reported incomplete state verification")
                        verified[role] = event
                    elif kind == "checkpoint":
                        checkpoints[role][event["frame"]] = event
                    elif kind == "progress" and time.monotonic() - last_progress >= 10:
                        print(f"{name}: {role} frame {event['frame']}, peer hash verified through {event['verified_frame']}", flush=True)
                        last_progress = time.monotonic()
                    elif kind == "diagnostic":
                        print(f"{name}: {role}: {event['message']}", flush=True)
                for role, process in processes.items():
                    if process.poll() is not None:
                        raise RuntimeError(f"{name}: {role} exited early ({process.returncode}); see {output / (role + '.log')}")
                if relay:
                    select.select([relay.socket], [], [], 0.001)
                else:
                    time.sleep(0.001)
            # Each peer already accepted the other's final periodic state hash.
            # Coordinate shutdown while both are servicing the live connection.
            stop.touch()
            for role, process in processes.items():
                result = process.wait(timeout=10)
                if result:
                    raise RuntimeError(f"{name}: {role} shutdown failed ({result})")
            for role in ("host", "join"):
                if set(checkpoints[role]) != {4149, 5149}:
                    raise RuntimeError(f"Missing {role} commercial-game checkpoint")
                for frame in (4149, 5149):
                    image = output / role / f"frame-{frame}.ppm"
                    data = image.read_bytes()
                    if not data.startswith(b"P6\n240 160\n255\n") or len(data.split(b"\n", 3)[3]) != 240 * 160 * 3:
                        raise RuntimeError(f"Invalid real framebuffer capture {image}")
                    checkpoints[role][frame]["framebuffer_sha256"] = sha(image)
                    if args.reference:
                        side = 0 if role == "host" else 1
                        reference = args.reference / f"frame-{frame}-p{side}.ppm"
                        if not reference.exists() or reference.read_bytes() != data:
                            raise RuntimeError(f"Network {role} framebuffer differs from direct paired execution at {frame}")
            stats = {"received": relay.received, "forwarded": relay.forwarded,
                     "dropped": relay.dropped, "maximum_queue": relay.maximum_queue} if relay else None
            if relay and (relay.dropped == 0 or relay.maximum_queue < 2):
                raise RuntimeError("Fault relay did not actually inject packet loss and overlapping delay")
            result = {"passed": True, "phase": name, "processes": 2, "frames_per_process": 5149,
                      "last_compared_hash_frame": 5100, "periodic_hashes_per_process": 85,
                      "elapsed_seconds": round(time.monotonic() - started, 3),
                      "relay": stats, "delay_ms": [30, 70] if fault else [0, 0],
                      "loss_probability": 0.05 if fault else 0,
                      "checkpoints": checkpoints}
            (output / "report.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
            print(f"PASS {name}: 2 processes × 5149 frames, 85 accepted peer state hashes each, {result['elapsed_seconds']} seconds", flush=True)
            return result
        finally:
            for process in processes.values():
                if process.poll() is None:
                    process.terminate()
            for process in processes.values():
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
            for thread in threads:
                thread.join(timeout=5)
            if relay:
                relay.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=pathlib.Path, required=True)
    parser.add_argument("--rom", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, default=pathlib.Path("artifacts/friendplay/fifa-network"))
    parser.add_argument("--reference", type=pathlib.Path, help="Optional direct-pair PPM checkpoint directory")
    parser.add_argument("--phase", choices=("all", "loopback", "jitter-loss"), default="all")
    parser.add_argument("--timeout", type=float, default=300)
    args = parser.parse_args()
    args.binary = args.binary.resolve(strict=True)
    args.rom = args.rom.resolve(strict=True)
    args.output = args.output.resolve()
    binary_digest = sha(args.binary)
    source = {args.rom: sha(args.rom), args.rom.with_suffix(".matchaboy.sav"): sha(args.rom.with_suffix(".matchaboy.sav"))}
    try:
        results = [run_phase(args, fault) for fault in (False, True)
                   if args.phase == "all" or args.phase == ("jitter-loss" if fault else "loopback")]
        args.output.mkdir(parents=True, exist_ok=True)
        report = args.output / "report.json"
        previous = json.loads(report.read_text(encoding="utf-8")) if report.exists() else {}
        phases = {item["phase"]: item for item in previous.get("results", [])} if (
            previous.get("rom_sha256") == source[args.rom] and previous.get("binary_sha256") == binary_digest) else {}
        phases.update({item["phase"]: item for item in results})
        if sha(args.binary) != binary_digest:
            raise RuntimeError("Probe binary changed during verification")
        report.write_text(json.dumps({"binary_sha256": binary_digest, "rom_sha256": source[args.rom], "results": list(phases.values())}, indent=2) + "\n", encoding="utf-8")
    finally:
        if any(sha(path) != digest for path, digest in source.items()):
            raise RuntimeError("Original cartridge or save changed during verification")


if __name__ == "__main__":
    main()
