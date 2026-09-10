"""Test the portable GBA player using an authored ARM ROM, no BIOS or game downloads."""
import argparse
import ctypes as C
from ctypes import wintypes as W
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
from verify_autopsy import png_rgb

# ARM7TDMI: increment SRAM[0] on boot, fill mode-3 video red; A/L/R select
# green/blue/white by reading the actual GBA KEYINPUT register.
CODE = bytes.fromhex('0e04a0e30010d0e5011081e20010c0e540509fe5b030d5e11f10a0e3010013e334109f05020c13e330109f05010c13e32c109f050604a0e3962ca0e3b210c0e0012052e2fcffff1a0103a0e314109fe5b010c0e1eeffffea30010004e0030000007c0000ff7f0000030400005352414d5f56313133')
def fixture():
    rom = bytearray(0xC0)
    rom[:4] = bytes.fromhex('2e0000ea')
    rom[0xB2] = 0x96
    return rom + CODE

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=False)
    user = C.WinDLL('user32')
    callback = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    user.EnumWindows.argtypes = [callback, W.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
    user.GetClassNameW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
    user.SendMessageTimeoutW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM, W.UINT, W.UINT, C.POINTER(C.c_size_t)]
    user.SendMessageTimeoutW.restype = C.c_ssize_t
    checks = []
    with tempfile.TemporaryDirectory(prefix='matcha portable ') as temp:
        root = Path(temp); binary = root/'Matchaboy.exe'
        shutil.copy2(args.binary, binary)
        rom = root/'test \u6d4b.gba'; rom.write_bytes(fixture())
        env = dict(os.environ, PATH=str(Path(os.environ['SystemRoot'])/'System32'))
        for boot in range(2):
            capture = output/f'boot-{boot}.png'; metadata = Path(str(capture)+'.json')
            with (output/f'boot-{boot}.log').open('w') as log:
                proc = subprocess.Popen([str(binary), str(rom), '--frames','10','--paused','--capture',str(capture)], cwd=root, env=env, stdout=log, stderr=log)
                found = []
                @callback
                def enum(hwnd, _):
                    pid = W.DWORD(); name = C.create_unicode_buffer(128)
                    user.GetWindowThreadProcessId(hwnd, C.byref(pid))
                    user.GetClassNameW(hwnd, name, len(name))
                    if pid.value == proc.pid and name.value == 'MatchaboyAutopsy': found.append(hwnd)
                    return True
                def wait(predicate):
                    deadline = time.monotonic()+15
                    while time.monotonic()<deadline:
                        value = predicate()
                        if value: return value
                        if proc.poll() is not None: raise RuntimeError(f'Player exited: {proc.returncode}')
                        time.sleep(.01)
                    raise TimeoutError('Player did not respond')
                def find():
                    user.EnumWindows(enum, 0)
                    return found[0] if found else None
                def send(msg, wp=0):
                    result = C.c_size_t()
                    if not user.SendMessageTimeoutW(hwnd,msg,wp,0,2,5000,C.byref(result)): raise RuntimeError('Message failed')
                def key(k, down=True): send(0x100 if down else 0x101,k)
                def snapshot():
                    old = metadata.stat().st_mtime_ns
                    key(0x7B); wait(lambda: metadata.stat().st_mtime_ns != old)
                    return json.loads(metadata.read_text())
                def pixel():
                    w,h,rgb = png_rgb(capture)
                    x,y = int(450*w/1280),int(450*h/920)
                    return tuple(rgb[(y*w+x)*3:(y*w+x)*3+3])
                try:
                    hwnd=wait(find); wait(metadata.exists)
                    state=snapshot(); assert state['platform']=='gba' and state['frames']==10
                    assert pixel()==(255,0,0), pixel()
                    if boot==0:
                        checks.append('Standalone EXE boots authored GBA ROM with Unicode path and no external BIOS')
                        for code, expected in [(ord('Z'),(0,255,0)),(ord('Q'),(0,0,255)),(ord('W'),(255,255,255))]:
                            key(code); key(0x20); time.sleep(.15); key(0x20)
                            snapshot(); assert pixel()==expected,(code,pixel())
                            key(code,False)
                        checks.append('A, L and R reach GBA hardware and change rendered pixels')
                        state=snapshot(); key(0x09)
                        after=snapshot(); assert not after['inspector_view'] and after['frames']==state['frames']
                        key(ord('C')); assert snapshot()['frames']==state['frames']
                        key(ord('C')); snapshot()
                        checks.append('Inspector is unavailable for GBA; controls toggle preserves paused machine')
                    send(0x10); assert proc.wait(timeout=5)==0
                    save=rom.with_suffix('.matchaboy.sav')
                    assert save.read_bytes()[0]==boot,(boot,save.read_bytes()[0])
                finally:
                    if proc.poll() is None: proc.kill(); proc.wait()
        checks.append('Cartridge save survives clean close and reload; second boot reads and increments saved SRAM')
    (output/'summary.json').write_text(json.dumps(dict(passed=True,checks=checks),indent=2))
    print(json.dumps(dict(passed=True,checks=checks),indent=2))
if __name__=='__main__': main()

