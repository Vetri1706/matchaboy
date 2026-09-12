"""Verify the licensed homebrew library from an isolated EXE-only directory."""
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
import traceback
from verify_autopsy import png_rgb

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    root=Path(__file__).resolve().parents[1]
    games=json.loads((root/'games/homebrew/manifest.json').read_text(encoding='utf-8'))['games']
    assert games, 'Homebrew catalog is empty'
    user=C.WinDLL('user32');cb=C.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
    user.EnumWindows.argtypes=[cb,W.LPARAM]
    user.GetWindowThreadProcessId.argtypes=[W.HWND,C.POINTER(W.DWORD)]
    user.GetClassNameW.argtypes=[W.HWND,W.LPWSTR,C.c_int]
    user.GetWindowTextW.argtypes=[W.HWND,W.LPWSTR,C.c_int]
    user.GetClientRect.argtypes=[W.HWND,C.POINTER(W.RECT)]
    user.GetMenu.argtypes=[W.HWND];user.GetMenu.restype=W.HMENU
    user.GetSubMenu.argtypes=[W.HMENU,C.c_int];user.GetSubMenu.restype=W.HMENU
    user.GetMenuItemCount.argtypes=[W.HMENU]
    user.GetMenuStringW.argtypes=[W.HMENU,W.UINT,W.LPWSTR,C.c_int,W.UINT]
    user.SendMessageTimeoutW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM,W.UINT,W.UINT,C.POINTER(C.c_size_t)]
    checks=[];reports=[];proc=None
    try:
        with ExitStack() as cleanup:
            temp=cleanup.enter_context(tempfile.TemporaryDirectory(prefix='Matchaboy arcade \u6d4b '))
            isolated=Path(temp);exe=isolated/'Matchaboy.exe';shutil.copy2(args.binary,exe)
            library_directory=(isolated/'isolated homebrew library').resolve()
            environment=dict(os.environ,PATH=str(Path(os.environ['SystemRoot'])/'System32'),
                             MATCHA_GAME_LIBRARY=str(library_directory))
            capture=out/'current.png';meta=Path(str(capture)+'.json')
            with (out/'app.log').open('w') as log:
                proc=subprocess.Popen([str(exe),'--frames','0','--paused','--capture',str(capture)],
                    cwd=isolated,env=environment,stdout=log,stderr=log)
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
                def send(msg,value=0,lp=0):
                    result=C.c_size_t()
                    if not user.SendMessageTimeoutW(hwnd,msg,value,lp,2,5000,C.byref(result)):raise RuntimeError('Window message failed')
                def key(value,down=True):
                    scans={0x25:(0x4b,True),0x26:(0x48,True),0x27:(0x4d,True),0x28:(0x50,True),
                           0x24:(0x47,True),0x23:(0x4f,True),0x1b:(0x01,False),0x0d:(0x1c,False),
                           0x09:(0x0f,False),0x7b:(0x58,False),0x20:(0x39,False),ord('D'):(0x20,False),
                           ord('A'):(0x1e,False),ord('W'):(0x11,False),ord('S'):(0x1f,False),
                           ord('L'):(0x26,False),ord('K'):(0x25,False),ord('Q'):(0x10,False),ord('I'):(0x17,False)}
                    scan,extended=scans[value]
                    flags=1|(scan<<16)|(int(extended)<<24)|(0 if down else (1<<30)|(1<<31))
                    send(0x100 if down else 0x101,value,flags)
                def tap(value):key(value);key(value,False)
                def select_card(index):
                    rect=W.RECT();assert user.GetClientRect(hwnd,C.byref(rect))
                    scale=min(rect.right/1280,rect.bottom/920)
                    # The catalog fits the first page; keyboard scrolling is
                    # independently checked if more games are added later.
                    first=max(0,index//2-4)*2
                    x=int((rect.right-1280*scale)/2+(32+(index%2)*394+185)*scale)
                    y=int((rect.bottom-920*scale)/2+(230+((index-first)//2)*122+55)*scale)
                    send(0x202,0,(y<<16)|x)
                def snap(name=None):
                    old=meta.stat().st_mtime_ns
                    tap(0x7b)
                    def read():
                        try:
                            if meta.stat().st_mtime_ns==old:return None
                            data=json.loads(meta.read_text(encoding='utf-8'))
                            return data if data['gpu_readback'] and 'library_count' in data else None
                        except (OSError,json.JSONDecodeError):return None
                    state=wait(read)
                    if name:shutil.copy2(capture,out/(name+'.png'))
                    return state
                def advance_until(target):
                    deadline=time.monotonic()+45
                    while time.monotonic()<deadline:
                        state=snap()
                        if state['frames']>=target:return state
                        time.sleep(.02)
                    raise TimeoutError(f"Game did not advance to frame {target}: {state}")
                def lcd_pixels(path,game):
                    w,h,pixels=png_rgb(path)
                    nw,nh=(240,160) if game['system']=='GBA' else (160,144)
                    height=533 if game['system']=='GBA' else 720
                    top=136+(720-height)//2
                    return b''.join(pixels[(y*w+x)*3:(y*w+x)*3+3]
                        for row in range(nh) for column in range(nw)
                        for x,y in [(int((48+(column+.5)*800/nw)*w/1280),
                                     int((top+(row+.5)*height/nh)*h/920))])
                def input_oracle(game):
                    plan=game['test'];captures={}
                    for variant in ('input','idle'):
                        target=out/(game['id']+'-control-'+variant+'.png')
                        # Fresh library/save locations make these independent
                        # executions of the same unchanged bundled cartridge.
                        env=dict(environment,MATCHA_GAME_LIBRARY=str((isolated/variant/game['id']).resolve()))
                        command=[str(exe),'--headless','--game',game['id'],'--capture',str(target)]
                        if variant=='input':
                            command += ['--frames',str(plan['boot_frames']),'--buttons',str(plan['input_mask']),
                                        '--input-frames',str(plan['input_frames'])]
                        else:
                            command += ['--frames',str(plan['boot_frames']+plan['input_frames'])]
                        with target.with_suffix('.log').open('w') as capture_log:
                            subprocess.run(command,cwd=isolated,env=env,stdout=capture_log,stderr=capture_log,
                                           timeout=45,check=True)
                        state=json.loads(Path(str(target)+'.json').read_text(encoding='utf-8'))
                        assert state['library_game']==games.index(game) and not state['inspector_view']
                        assert state['buttons']==(plan['input_mask'] if variant=='input' else 0)
                        captures[variant]=lcd_pixels(target,game)
                    count=sum(captures['input'][i:i+3]!=captures['idle'][i:i+3]
                              for i in range(0,len(captures['input']),3))
                    assert count>50,(game['id'],'Input and equal-duration idle execution have the same LCD',count)
                    return count
                initial=snap('library');assert initial['platform']=='library' and initial['library_game']==-1
                assert initial['frames']==0 and initial['library_view']
                assert initial['keyboard_mapping']==[2,0,13,1,37,40,49,36,12,34]
                assert initial['library_count']==len(games)
                file_menu=user.GetSubMenu(user.GetMenu(hwnd),0)
                included=user.GetSubMenu(file_menu,2)
                assert included and user.GetMenuItemCount(included)==len(games)
                credits=C.create_unicode_buffer(128)
                assert user.GetMenuStringW(file_menu,1053,credits,len(credits),0)>0
                assert credits.value.replace('&','')=='Game credits and licenses'
                for i,game in enumerate(games):
                    label=C.create_unicode_buffer(256)
                    assert user.GetMenuStringW(included,i,label,len(label),0x400)>0
                    assert label.value==game['title']
                checks.append('Native File menu exposes every full homebrew title for keyboard and accessibility navigation')
                checks.append('EXE-only Unicode directory opens real library without ROM chooser or external game files')
                for i,game in enumerate(games):
                    send(0x111,1050)
                    tap(0x24)
                    for _ in range(i//2):tap(0x28)
                    if i%2:tap(0x27)
                    selected=snap(game['id']+'-credits')
                    assert selected['library_view'] and selected['library_selection']==i
                    assert selected['library_id']==game['id'] and selected['library_title']==game['title']
                    assert selected['library_author']==game['author'] and selected['library_license']==game['license']
                    assert selected['library_players']==game['players'] and selected['library_source']==game['source']
                    caption=C.create_unicode_buffer(512);user.GetWindowTextW(hwnd,caption,len(caption))
                    assert game['title'] in caption.value and f'({i+1} of {len(games)})' in caption.value
                    if i<10:
                        tap(0x24);select_card(i);assert snap()['library_selection']==i
                    before=selected['frames'];time.sleep(.1)
                    send(0x111,1013) # disabled frame command must not run an old game.
                    assert snap()['frames']==before
                    plan=game['test'];boot_frames=plan['boot_frames'];input_mask=plan['input_mask'];input_frames=plan['input_frames']
                    assert boot_frames>0 and 0<input_mask<1024 and input_frames>0
                    send(0x111,1052);advance_until(boot_frames);tap(0x1b)
                    title=snap(game['id']+'-title')
                    assert not title['library_view'] and title['library_game']==i and title['paused']
                    assert (title.get('platform')=='gba') == (game['system']=='GBA')
                    # Per-title entry actions come from the verified release's
                    # test plan and use real mapped keyboard events.
                    keys=[ord('D'),ord('A'),ord('W'),ord('S'),ord('L'),ord('K'),0x20,0x0d,ord('Q'),ord('I')]
                    tap(0x1b)
                    for bit,value in enumerate(keys):
                        if input_mask&(1<<bit):key(value)
                    assert snap()['buttons']==input_mask
                    advance_until(title['frames']+input_frames)
                    for bit,value in enumerate(keys):
                        if input_mask&(1<<bit):key(value,False)
                    advance_until(title['frames']+input_frames+12);tap(0x1b)
                    play=snap(game['id']+'-play')
                    assert play['frames']>title['frames'] and play['paused']
                    w,h,a=png_rgb(out/(game['id']+'-title.png'));_,_,b=png_rgb(out/(game['id']+'-play.png'))
                    # LCD-only samples in the actual scaled window. Exclude
                    # host frame counters, controls and all other UI changes.
                    native_width,native_height=(240,160) if game['system']=='GBA' else (160,144)
                    display_height=533 if game['system']=='GBA' else 720
                    top=136+(720-display_height)//2
                    different=sum(a[(y*w+x)*3:(y*w+x)*3+3]!=b[(y*w+x)*3:(y*w+x)*3+3]
                        for row in range(native_height) for column in range(native_width)
                        for x,y in [(int((48+(column+.5)*800/native_width)*w/1280),
                                     int((top+(row+.5)*display_height/native_height)*h/920))])
                    assert different>50,(game['id'],'Entry action did not change game display',different)
                    key(ord('D'));assert snap()['buttons']&1;key(ord('D'),False)
                    tap(9);inspector=snap();assert inspector['inspector_view'] and inspector['frames']==play['frames']
                    send(0x111,1050);library_state=snap();assert library_state['library_view'] and library_state['buttons']==0
                    time.sleep(.15);assert snap()['frames']==library_state['frames']
                    send(0x111,1051);back=snap();assert back['paused'] and back['frames']==play['frames']
                    assert back['inspector_view'];tap(9)
                    # Running game is suspended in Library and resumes only on return.
                    tap(0x1b);advance_until(play['frames']+3);send(0x111,1050);held=snap()
                    time.sleep(.15);assert snap()['frames']==held['frames']
                    send(0x111,1051);advance_until(held['frames']+3);send(0x111,1050)
                    assert snap()['frames']>held['frames']
                    prepared=library_directory/Path(game['rom']).name
                    assert prepared.read_bytes()==(root/game['rom']).read_bytes()
                    digest=hashlib.sha256(prepared.read_bytes()).hexdigest();assert digest==game['sha256']
                    input_changes=input_oracle(game)
                    reports.append(dict(id=game['id'],system=game['system'],passed=True,input_changes_lcd=different,
                                        input_vs_idle_changes=input_changes,rom_sha256=digest,test=plan))
                tap(0x23);assert snap()['library_selection']==len(games)-1
                tap(0x27);tap(0x28);assert snap()['library_selection']==len(games)-1
                tap(0x24);tap(0x25);tap(0x26);assert snap()['library_selection']==0
                checks.append('Row-major library navigation stays within the actual catalog bounds')
                checks.append('Every bundled homebrew release boots, changes its LCD after entry input, accepts mapped keys and opens Inspector')
                checks.append('Fresh paired cartridge executions distinguish declared input from equal-duration idle animation')
                checks.append('Every selected game displays its author, license, player count and official project link')
                checks.append('Library blocks emulation/debug stepping, releases buttons, preserves paused state and resumes previously running games')
                checks.append('Prepared ROMs byte-match their source artifacts, without downloads or installed game assets')
                send(0x10);assert proc.wait(timeout=5)==0
        result=dict(passed=True,checks=checks,games=reports)
    except Exception as error:
        result=dict(passed=False,checks=checks,games=reports,error=str(error),traceback=traceback.format_exc())
    finally:
        if proc and proc.poll() is None:proc.terminate();proc.wait(timeout=5)
    result['binary_sha256']=hashlib.sha256(args.binary.read_bytes()).hexdigest()
    (out/'summary.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
    return 0 if result['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
