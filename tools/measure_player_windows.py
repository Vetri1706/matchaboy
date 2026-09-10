"""Measure real emulated frames/second through an actual Windows player window."""
import argparse
import ctypes as C
from ctypes import wintypes as W
import json
from pathlib import Path
import subprocess
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary', type=Path, required=True)
p.add_argument('--rom', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--seconds', type=float, default=10)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
capture = a.output.resolve() / 'frame.png'
meta = Path(str(capture) + '.json')
u = C.WinDLL('user32')
callback = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
u.EnumWindows.argtypes = [callback, W.LPARAM]
u.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
u.GetClassNameW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
u.SendMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
proc = subprocess.Popen([str(a.binary.resolve()), str(a.rom.resolve()), '--frames', '120', '--paused', '--capture', str(capture)])
found = []
@callback
def enum(hwnd, _):
    pid = W.DWORD()
    u.GetWindowThreadProcessId(hwnd, C.byref(pid))
    name = C.create_unicode_buffer(128)
    u.GetClassNameW(hwnd, name, len(name))
    if pid.value == proc.pid and name.value == 'MatchaboyAutopsy':
        found.append(hwnd)
    return True
def wait(predicate):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        result = predicate()
        if result: return result
        if proc.poll() is not None: raise RuntimeError('Player exited')
        time.sleep(.01)
    raise TimeoutError('Player did not respond')
try:
    def find():
        u.EnumWindows(enum, 0)
        return found[0] if found else None
    hwnd = wait(find)
    wait(meta.exists)
    def key(k): u.SendMessageW(hwnd, 0x100, k, 0)
    def snapshot():
        previous = meta.stat().st_mtime_ns
        key(0x7B)
        wait(lambda: meta.stat().st_mtime_ns != previous)
        return json.loads(meta.read_text())
    start = snapshot()
    begin = time.perf_counter()
    key(0x20)
    time.sleep(a.seconds)
    key(0x20)
    elapsed = time.perf_counter() - begin
    end = snapshot()
    fps = (end['frames'] - start['frames']) / elapsed
    report = dict(binary=str(a.binary.resolve()), seconds=elapsed,
                  frames=end['frames']-start['frames'], fps=fps,
                  target_fps=4194304/70224, speed_percent=fps/(4194304/70224)*100)
    (a.output / 'speed.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))
finally:
    if found and proc.poll() is None: u.SendMessageW(found[0], 0x10, 0, 0)
    try: proc.wait(timeout=5)
    except subprocess.TimeoutExpired: proc.kill()
