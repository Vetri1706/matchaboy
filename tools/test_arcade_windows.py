"""Verify the embedded ten-game library from an isolated EXE-only directory."""
import argparse
from contextlib import ExitStack
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
from verify_autopsy import png_rgb

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    root=Path(__file__).resolve().parents[1]
    games=[]
    for system in ['gb','gba']:games.extend(json.loads((root/'games'/system/'manifest.json').read_text())['games'])
    assert len(games)==10
    user=C.WinDLL('user32');cb=C.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
    user.EnumWindows.argtypes=[cb,W.LPARAM]
    user.GetWindowThreadProcessId.argtypes=[W.HWND,C.POINTER(W.DWORD)]
    user.GetClassNameW.argtypes=[W.HWND,W.LPWSTR,C.c_int]
    user.SendMessageTimeoutW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM,W.UINT,W.UINT,C.POINTER(C.c_size_t)]
    checks=[];reports=[];proc=None
    try:
        with ExitStack() as cleanup:
            temp=cleanup.enter_context(tempfile.TemporaryDirectory(prefix='Matchaboy arcade \u6d4b '))
            isolated=Path(temp);exe=isolated/'Matchaboy.exe';shutil.copy2(args.binary,exe)
            capture=out/'current.png';meta=Path(str(capture)+'.json')
            with (out/'app.log').open('w') as log:
                proc=subprocess.Popen([str(exe),'--frames','0','--paused','--capture',str(capture)],
                    cwd=isolated,env=dict(os.environ,PATH=str(Path(os.environ['SystemRoot'])/'System32')),stdout=log,stderr=log)
                def stop():
                    if proc.poll() is None:proc.terminate();proc.wait(timeout=5)
                cleanup.callback(stop)
                found=[]
                @cb
                def enum(hwnd,_):
                    pid=W.DWORD();name=C.create_unicode_buffer(128)
                    user.GetWindowThreadProcessId(hwnd,C.byref(pid));user.GetClassNameW(hwnd,name,len(name))
                    if pid.value==proc.pid and name.value=='MatchaboyAutopsy':found.append(hwnd)
                    return True
                def wait(fn):
                    deadline=time.monotonic()+15
                    while time.monotonic()<deadline:
                        result=fn()
                        if result:return result
                        if proc.poll() is not None:raise RuntimeError('Player exited unexpectedly')
                        time.sleep(.02)
                    raise TimeoutError('Library did not respond')
                def find():user.EnumWindows(enum,0);return found[0] if found else None
                hwnd=wait(find);wait(meta.exists)
                def send(msg,value=0):
                    result=C.c_size_t()
                    if not user.SendMessageTimeoutW(hwnd,msg,value,0,2,5000,C.byref(result)):raise RuntimeError('Window message failed')
                def key(value,down=True):send(0x100 if down else 0x101,value)
                def tap(value):key(value);key(value,False)
                def snap(name=None):
                    old=meta.stat().st_mtime_ns
                    tap(0x7b)
                    def read():
                        try:
                            if meta.stat().st_mtime_ns==old:return None
                            data=json.loads(meta.read_text())
                            return data if data['gpu_readback'] else None
                        except (OSError,json.JSONDecodeError):return None
                    state=wait(read)
                    if name:shutil.copy2(capture,out/(name+'.png'))
                    return state
                initial=snap('library');assert initial['platform']=='library' and initial['library_game']==-1
                assert initial['frames']==0 and initial['library_view']
                checks.append('EXE-only Unicode directory opens real library without ROM chooser or external game files')
                for i,game in enumerate(games):
                    send(0x111,1050)
                    tap(0x25)
                    for _ in range(5):tap(0x26)
                    if i>=5:tap(0x27)
                    for _ in range(i%5):tap(0x28)
                    selected=snap()
                    assert selected['library_view'] and selected['library_selection']==i
                    before=selected['frames'];time.sleep(.1)
                    send(0x111,1013) # disabled frame command must not run an old game.
                    assert snap()['frames']==before
                    send(0x111,1052);time.sleep(.35);tap(0x20)
                    title=snap(game['id']+'-title')
                    assert not title['library_view'] and title['library_game']==i and title['paused']
                    assert (title.get('platform')=='gba') == (game['system']=='GBA')
                    # Start is a real cartridge key, with held input across emulator frames.
                    key(0x0d);tap(0x20);time.sleep(.18);key(0x0d,False);time.sleep(.25);tap(0x20)
                    play=snap(game['id']+'-play')
                    assert play['frames']>title['frames'] and play['paused']
                    w,h,a=png_rgb(out/(game['id']+'-title.png'));_,_,b=png_rgb(out/(game['id']+'-play.png'))
                    # LCD-only samples: exclude host frame counters/chrome.
                    different=sum(a[(y*w+x)*3:(y*w+x)*3+3]!=b[(y*w+x)*3:(y*w+x)*3+3]
                        for y in range(300,750,3) for x in range(100,800,3))
                    assert different>50,(game['id'],'Start did not change game display',different)
                    key(0x27);assert snap()['buttons']&1;key(0x27,False)
                    tap(9);inspector=snap();assert inspector['inspector_view'] and inspector['frames']==play['frames']
                    send(0x111,1050);library=snap();assert library['library_view'] and library['buttons']==0
                    time.sleep(.15);assert snap()['frames']==library['frames']
                    send(0x111,1051);back=snap();assert back['paused'] and back['frames']==play['frames']
                    assert back['inspector_view'];tap(9)
                    # Running game is suspended in Library and resumes only on return.
                    tap(0x20);time.sleep(.1);send(0x111,1050);held=snap()
                    time.sleep(.15);assert snap()['frames']==held['frames']
                    send(0x111,1051);time.sleep(.1);send(0x111,1050)
                    assert snap()['frames']>held['frames']
                    prepared=isolated/'Matchaboy Data/Library'/Path(game['rom']).name
                    assert prepared.read_bytes()==(root/game['rom']).read_bytes()
                    reports.append(dict(id=game['id'],system=game['system'],passed=True,start_changes_lcd=different,
                                        rom_sha256=hashlib.sha256(prepared.read_bytes()).hexdigest()))
                checks.append('All ten embedded games launch, show distinct title/play screens, accept cartridge input and open Inspector')
                checks.append('Library blocks emulation/debug stepping, releases buttons, preserves paused state and resumes previously running games')
                checks.append('Prepared ROMs byte-match their source artifacts, without downloads or installed game assets')
                send(0x10);assert proc.wait(timeout=5)==0
        result=dict(passed=True,checks=checks,games=reports)
    except Exception as error:
        result=dict(passed=False,checks=checks,games=reports,error=str(error))
    finally:
        if proc and proc.poll() is None:proc.terminate();proc.wait(timeout=5)
    result['binary_sha256']=hashlib.sha256(args.binary.read_bytes()).hexdigest()
    (out/'summary.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
    return 0 if result['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
