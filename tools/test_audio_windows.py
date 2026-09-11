"""Exercise GB/GBA PCM playback, mute, pause and Windows device completion."""
import argparse
import ctypes as C
from ctypes import wintypes as W
import json
from pathlib import Path
import subprocess
import time
from make_audio_demo import build
from test_gba_windows import fixture

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--require-continuity', action='store_true', help='Require no device starvation/dropouts on an otherwise idle host')
    parser.add_argument('--system', choices=['both','gb','gba'], default='both')
    parser.add_argument('--inspector', action='store_true')
    parser.add_argument('--seconds', type=float, default=8)
    parser.add_argument('--require-device',action='store_true')
    args=parser.parse_args(); output=args.output.resolve(); output.mkdir(parents=True,exist_ok=False)
    # Enable the GBA's own PSG square channel, routed to both speakers. The
    # appended authored fixture still exercises real display and cartridge RAM.
    gba=fixture()
    tone=bytes.fromhex('0103a0e38010a0e3b418c0e114109fe5b018c0e110109fe5b216c0e10c109fe5b416c0e1020000ea7711000080f00000d6860000')
    gba=gba[:0xC0]+tone+gba[0xC0:]
    user=C.WinDLL('user32'); cb=C.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
    user.EnumWindows.argtypes=[cb,W.LPARAM]
    user.GetWindowThreadProcessId.argtypes=[W.HWND,C.POINTER(W.DWORD)]
    user.GetClassNameW.argtypes=[W.HWND,W.LPWSTR,C.c_int]
    user.PostMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM]
    user.SendMessageTimeoutW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM,W.UINT,W.UINT,C.POINTER(C.c_size_t)]
    user.SendMessageTimeoutW.restype=C.c_ssize_t
    reports={}
    for system,rom in [('gb',build()[0]),('gba',gba)]:
        if args.system != 'both' and system != args.system: continue
        path=output/f'tone.{system}'; path.write_bytes(rom)
        capture=output/f'{system}.png'; meta=Path(str(capture)+'.audio.json')
        with (output/f'{system}.log').open('w') as log:
            proc=subprocess.Popen([str(args.binary.resolve()),str(path),'--frames','120','--paused','--capture',str(capture)] + (['--inspector'] if args.inspector else []),stdout=log,stderr=log)
            found=[]
            @cb
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
                    if proc.poll() is not None: raise RuntimeError(f'Player exited {proc.returncode}')
                    time.sleep(.01)
                raise TimeoutError('Player did not respond')
            def find():
                user.EnumWindows(enum,0)
                return found[0] if found else None
            def send(message,wp=0):
                result=C.c_size_t()
                if not user.SendMessageTimeoutW(hwnd,message,wp,0,2,5000,C.byref(result)): raise RuntimeError('Message failed')
            def key(k): send(0x100,k)
            def snap():
                old=meta.stat().st_mtime_ns if meta.exists() else 0
                key(0x7B)
                def completed_capture():
                    # F12 schedules a paint; the timestamp can change before
                    # the writer finishes the JSON sidecar.
                    try:
                        if meta.stat().st_mtime_ns == old: return None
                        return json.loads(meta.read_text())
                    except (OSError, json.JSONDecodeError):
                        return None
                return wait(completed_capture)
            try:
                hwnd=wait(find); start=snap()
                if args.require_device: assert start['device_open'],'No Windows audio output device'
                key(0x20); time.sleep(args.seconds)
                running=snap()
                if args.require_device: assert running['device_open'], running
                assert running['generated_frames']>24000 and running['generated_peak']>100
                if running['device_open']:
                    assert running['completed_frames']>24000 and running['peak']>50
                    if args.require_continuity:
                        assert running['underruns'] == 0, running
                        assert running['dropped_frames'] == 0, running
                key(ord('M')); muted=snap()
                time.sleep(.2); silent=snap()
                assert silent['muted'] and silent['queued_buffers']==0
                assert silent['submitted_frames']==muted['submitted_frames']
                assert silent['generated_frames']>muted['generated_frames']
                key(ord('M')); time.sleep(.2); resumed=snap()
                if resumed['device_open']: assert resumed['submitted_frames']>silent['submitted_frames']
                key(0x20); paused=snap(); time.sleep(.2); still=snap()
                assert still['paused'] and still['queued_buffers']==0
                assert still['generated_frames']==paused['generated_frames']
                send(0x10); assert proc.wait(timeout=5)==0
                reports[system]=dict(passed=True,playback=running,mute_clears_queue=True,pause_stops_audio=True)
            finally:
                if proc.poll() is None: proc.kill();proc.wait()
    (output/'summary.json').write_text(json.dumps(reports,indent=2))
    print(json.dumps(reports,indent=2))
if __name__=='__main__': main()
