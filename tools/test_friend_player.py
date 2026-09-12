#!/usr/bin/env python3
"""Exercise the actual desktop player's paired headless friend-play entry points."""
import argparse
import hashlib
import json
from pathlib import Path
import secrets
import shutil
import socket
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve()
    if binary.suffix == '.app':
        binary /= 'Contents/MacOS/MatchaAutopsy'
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    report = {'passed': False, 'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(), 'checks': []}
    try:
        with tempfile.TemporaryDirectory(prefix='Matchaboy friend app ') as temporary:
            work = Path(temporary)
            for name, source in [('gb', ROOT/'games/gb/roms/moon-courier.gb'),
                                 ('gba', ROOT/'games/gba/roms/drift-circuit.gba')]:
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                    sock.bind(('127.0.0.1', 0))
                    port = sock.getsockname()[1]
                code = secrets.token_hex(16)
                stop = work/(name+'-stop')
                processes, logs, streams = [], [], []
                try:
                    for side in range(2):
                        folder = work/f'{name}-{side}'
                        folder.mkdir()
                        rom = folder/source.name
                        shutil.copyfile(source, rom)
                        image = output/f'{name}-{side}.png'
                        log = output/f'{name}-{side}.log'
                        stream = log.open('w')
                        streams.append(stream)
                        network = ['--net-host', str(port)] if side == 0 else ['--net-join', f'127.0.0.1:{port}']
                        command = [str(binary), str(rom), '--headless', '--frames', '120', '--capture', str(image),
                                   '--net-code', code, '--net-stop-file', str(stop), *network]
                        processes.append(subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT, cwd=folder))
                        logs.append(log)
                    deadline = time.monotonic()+45
                    while not all('Friend capture ready: frame 120' in log.read_text() for log in logs):
                        if time.monotonic()>deadline or any(p.poll() is not None for p in processes):
                            raise RuntimeError('Paired app did not reach 120 frames: '+str([p.poll() for p in processes]))
                        time.sleep(0.01)
                    time.sleep(0.25)  # Both peers service their last state hash before coordinated close.
                    stop.touch()
                    for side, process in enumerate(processes):
                        if process.wait(timeout=10):
                            raise RuntimeError(logs[side].read_text())
                        image = output/f'{name}-{side}.png'
                        metadata = json.loads(Path(str(image)+'.json').read_text())
                        state = metadata['netplay']
                        if state['frames']!=120 or state['verified_frames']!=120:
                            raise RuntimeError('Actual app did not compare its final peer state: '+str(state))
                        if not image.read_bytes().startswith(b'\x89PNG\r\n\x1a\n'):
                            raise RuntimeError('Actual app did not render its framebuffer')
                        report['checks'].append({'platform': name, 'side': side, 'netplay': state})
                finally:
                    for process in processes:
                        if process.poll() is None:
                            process.terminate()
                        try:
                            process.wait(timeout=3)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait()
                    for stream in streams:
                        stream.close()
            for options in [['--net-host', '0'], ['--net-join', '127.0.0.1:no'],
                            ['--net-host', '27888', '--paused'], ['--net-code', 'wrong'],
                            ['--net-host', '27888', '--net-code', 'x'*32]]:
                result = subprocess.run([str(binary), str(ROOT/'games/gb/roms/moon-courier.gb'),
                                         '--headless', '--capture', str(work/'bad.png'), *options],
                                        capture_output=True, text=True, timeout=5)
                if result.returncode!=1:
                    raise RuntimeError('Invalid friend options were accepted')
                report['checks'].append({'rejected_options': options, 'exit_code': result.returncode})
        report['passed'] = hashlib.sha256(binary.read_bytes()).hexdigest()==report['binary_sha256']
        if not report['passed']:
            raise RuntimeError('App binary changed during verification')
    finally:
        report['seconds'] = time.monotonic()-started
        (output/'summary.json').write_text(json.dumps(report, indent=2)+'\n')
    print('PASS four actual desktop app peers with 120 verified frames each; five invalid connection options')


if __name__ == '__main__':
    main()
