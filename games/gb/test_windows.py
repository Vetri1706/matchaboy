#!/usr/bin/env python3
"""Native Matchaboy display and real Windows speaker smoke test for each GB game.

SPDX-License-Identifier: GPL-3.0-only
The full objectives/input replays live in test_games.py. This separate check runs
the actual desktop application, captures its OpenGL output, and requires nonzero
PCM and completed WinMM device buffers. It is not a subjective listening test.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
from pathlib import Path
import subprocess
import time

ROOT=Path(__file__).resolve().parent


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args(); binary=args.binary.resolve(); output=args.output.resolve()
    output.mkdir(parents=True,exist_ok=True)
    user=C.WinDLL('user32'); callback=C.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
    user.EnumWindows.argtypes=[callback,W.LPARAM]
    user.GetWindowThreadProcessId.argtypes=[W.HWND,C.POINTER(W.DWORD)]
    user.GetClassNameW.argtypes=[W.HWND,W.LPWSTR,C.c_int]
    user.SendMessageTimeoutW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM,W.UINT,W.UINT,C.POINTER(C.c_size_t)]
    user.SendMessageTimeoutW.restype=C.c_ssize_t
    reports=[]
    for game in json.loads((ROOT/'manifest.json').read_text())['games']:
        rom=ROOT/'roms'/f'{game["id"]}.gb'
        capture=output/f'{game["id"]}-native.png'
        audio_meta=Path(str(capture)+'.audio.json'); capture_meta=Path(str(capture)+'.json')
        with (output/f'{game["id"]}.log').open('w') as log:
            proc=subprocess.Popen([str(binary),str(rom),'--frames','30','--paused','--capture',str(capture)],stdout=log,stderr=log)
            found=[]
            @callback
            def enum(hwnd,_):
                pid=W.DWORD(); name=C.create_unicode_buffer(128)
                user.GetWindowThreadProcessId(hwnd,C.byref(pid)); user.GetClassNameW(hwnd,name,len(name))
                if pid.value==proc.pid and name.value=='MatchaboyAutopsy': found.append(hwnd)
                return True
            def wait(predicate):
                deadline=time.monotonic()+15
                while time.monotonic()<deadline:
                    value=predicate()
                    if value: return value
                    if proc.poll() is not None: raise RuntimeError(f'App exited {proc.returncode}')
                    time.sleep(.01)
                raise TimeoutError(f'{game["id"]}: desktop did not respond')
            def find():
                user.EnumWindows(enum,0)
                return found[0] if found else None
            def send(message,wp=0):
                result=C.c_size_t()
                if not user.SendMessageTimeoutW(hwnd,message,wp,0,2,5000,C.byref(result)): raise RuntimeError('Window message failed')
            def key(k,down=True): send(0x100 if down else 0x101,k)
            def snap():
                old=audio_meta.stat().st_mtime_ns if audio_meta.exists() else 0
                key(0x7B); key(0x7B,False)
                def ready():
                    try:
                        if audio_meta.stat().st_mtime_ns==old: return None
                        return json.loads(audio_meta.read_text())
                    except (OSError,json.JSONDecodeError): return None
                return wait(ready)
            try:
                hwnd=wait(find); before=snap()
                assert before['device_open'],'No available Windows speaker device'
                key(0x0D)       # Real hardware Start input remains down over a frame.
                key(0x20); key(0x20,False)  # Resume emulator.
                time.sleep(.18); key(0x0D,False)
                time.sleep(2.0)
                running=snap()
                assert running['generated_frames']>24000 and running['generated_peak']>100,running
                assert running['completed_frames']>24000 and running['peak']>50,running
                pixels=json.loads(capture_meta.read_text())
                assert pixels['gpu_readback'],pixels
                reports.append({'id':game['id'],'passed':True,'rom_sha256':game['sha256'],
                                'speaker':running,'native_capture':capture.name,
                                'display_gpu_readback':True})
                send(0x10); assert proc.wait(timeout=5)==0
                print(f'PASS native display + speaker: {game["id"]}',flush=True)
            finally:
                if proc.poll() is None: proc.kill(); proc.wait()
    report={'binary':str(binary),'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
            'results':reports,'limitations':['Automated device completion and PCM amplitude, not subjective listening.','Short native smoke test, not a full human playthrough.']}
    (output/'summary.json').write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__': main()
