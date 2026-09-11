"""Boot all five original GBA ROMs in the native Matchaboy player and capture them.
SPDX-License-Identifier: GPL-3.0-only
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time
from verify_autopsy import png_rgb

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();output=args.output.resolve();output.mkdir(parents=True,exist_ok=False)
    repo=Path(__file__).resolve().parents[1]
    manifest=json.loads((repo/'games/gba/manifest.json').read_text())['games']
    user=C.WinDLL('user32');callback=C.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
    user.EnumWindows.argtypes=[callback,W.LPARAM]
    user.GetWindowThreadProcessId.argtypes=[W.HWND,C.POINTER(W.DWORD)]
    user.GetClassNameW.argtypes=[W.HWND,W.LPWSTR,C.c_int]
    user.SendMessageTimeoutW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM,W.UINT,W.UINT,C.POINTER(C.c_size_t)]
    user.SendMessageTimeoutW.restype=C.c_ssize_t
    reports=[]
    for kind,game in enumerate(manifest):
        capture=output/(game['id']+'.png');meta=Path(str(capture)+'.json')
        rom=repo/game['rom']
        with (output/(game['id']+'.log')).open('w') as log:
            proc=subprocess.Popen([str(args.binary.resolve()),str(rom),'--frames','60','--paused','--capture',str(capture)],stdout=log,stderr=log)
            def wait(fn):
                deadline=time.monotonic()+20
                while time.monotonic()<deadline:
                    value=fn()
                    if value:return value
                    if proc.poll() is not None:raise RuntimeError(f'Player exited {proc.returncode}')
                    time.sleep(.01)
                raise TimeoutError(game['id'])
            def find():
                found=[]
                @callback
                def enum(hwnd,_):
                    pid=W.DWORD();name=C.create_unicode_buffer(128)
                    user.GetWindowThreadProcessId(hwnd,C.byref(pid));user.GetClassNameW(hwnd,name,len(name))
                    if pid.value==proc.pid and name.value=='MatchaboyAutopsy':found.append(hwnd)
                    return True
                user.EnumWindows(enum,0);return found[0] if found else None
            def send(msg,wp=0):
                r=C.c_size_t()
                if not user.SendMessageTimeoutW(hwnd,msg,wp,0,2,5000,C.byref(r)):raise RuntimeError('Window message failed')
            def key(code,down=True):send(0x100 if down else 0x101,code)
            def snapshot():
                old=meta.stat().st_mtime_ns if meta.exists() else 0;key(0x7B)
                def fresh():
                    try:
                        if meta.stat().st_mtime_ns==old:return None
                        return json.loads(meta.read_text())
                    except (OSError,json.JSONDecodeError):return None
                return wait(fresh)
            def game_pixels():
                w,h,rgb=png_rgb(capture);parts=[]
                for y in range(int(h*.27),int(h*.8)):
                    parts.append(rgb[(y*w+int(w*.08))*3:(y*w+int(w*.64))*3])
                return b''.join(parts)
            try:
                hwnd=wait(find);wait(meta.exists)
                title=snapshot();assert title['platform']=='gba' and title['memory_first']==kind
                title_pixels=game_pixels();colors=set(zip(title_pixels[0::3],title_pixels[1::3],title_pixels[2::3]));assert len(colors)>=5,(game['id'],colors)
                # The known player layout starts the GBA screen at (48,229).
                # Every original title has an uninterrupted solid top row. This
                # catches missing volatile DMA source writes as alternating stripes.
                w,h,rgb=png_rgb(capture);row=int(231*h/920)
                solid={tuple(rgb[(row*w+x)*3:(row*w+x)*3+3]) for x in range(int(52*w/1280),int(842*w/1280))}
                assert solid=={(16,33,57)},(game['id'],'DMA fill is not solid',solid)
                shutil.copy2(capture,output/(game['id']+'-title.png'))
                key(0x0D)
                for _ in range(6):send(0x111,1013)
                key(0x0D,False)
                key(ord('Z'))
                for _ in range(5):send(0x111,1013)
                key(ord('Z'),False)
                play=snapshot()
                if play['inspector_view']:key(0x09);play=snapshot()
                assert play['frames']>=title['frames']+11
                assert game_pixels()!=title_pixels
                shutil.copy2(capture,output/(game['id']+'-play.png'))
                # Native speaker output is observed through completed OS buffers,
                # independently of the generated PCM exercised by the probe.
                key(0x20);time.sleep(1.25);snapshot();key(0x20)
                audio=json.loads(Path(str(capture)+'.audio.json').read_text())
                assert audio['generated_peak']>100 and audio['generated_frames']>24000,audio
                if audio['device_open']:assert audio['completed_frames']>10000 and audio['peak']>100,audio
                send(0x10);assert proc.wait(timeout=10)==0
                reports.append(dict(id=game['id'],passed=True,sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),title_colors=len(colors),title=title,play=play,audio=audio))
            finally:
                if proc.poll() is None:proc.kill();proc.wait()
    summary=dict(passed=True,scope='Native Windows player boots, displays original title/art, starts gameplay through hardware input, steps frames and produces nonzero PCM; completed Windows device buffers checked when a device is available. No subjective listening or human playtest claim.',games=reports)
    (output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
if __name__=='__main__':main()
