"""Verify actual hybrid-GPU selection via the native menu and process restart."""
import argparse, ctypes as C, json, shutil, subprocess, time, winreg
# Run in a normal user session: this integration test writes only an isolated
# test executable's HKCU GPU entry and deletes it afterward. Restricted CI
# sessions may deny registry writes; do not run it as an administrator.
from ctypes import wintypes as W
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
a=p.parse_args(); out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
exe=out/'Matchaboy.exe';shutil.copy2(a.binary,exe)
rom=out/'gpu-fixture.gb';data=bytearray(32768);data[0x100:0x103]=bytes([0,0x18,0xFD]);rom.write_bytes(data)
capture=out/'gpu.png';meta=Path(str(capture)+'.gpu.json')
u=C.WinDLL('user32');cb=C.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
u.EnumWindows.argtypes=[cb,W.LPARAM];u.GetWindowThreadProcessId.argtypes=[W.HWND,C.POINTER(W.DWORD)]
u.GetClassNameW.argtypes=[W.HWND,W.LPWSTR,C.c_int]
u.SendMessageTimeoutW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM,W.UINT,W.UINT,C.POINTER(C.c_size_t)]
key=winreg.CreateKey(winreg.HKEY_CURRENT_USER,r'Software\Microsoft\DirectX\UserGpuPreferences')
# This isolated test executable has no user settings. Preserve an unrelated token
# to verify the menu modifies only its own GPU preference field.
winreg.SetValueEx(key,str(exe),0,winreg.REG_SZ,'SwapEffectUpgradeEnable=0;')
proc=None;reports=[]
def wait(fn):
 end=time.monotonic()+15
 while time.monotonic()<end:
  value=fn()
  if value:return value
  if proc.poll() is not None:raise RuntimeError(f'App exited {proc.returncode}')
  time.sleep(.02)
 raise TimeoutError('GPU test window did not respond')
def send(hwnd,msg,value):
 result=C.c_size_t()
 if not u.SendMessageTimeoutW(hwnd,msg,value,0,2,5000,C.byref(result)):raise RuntimeError('GPU menu did not respond')
def launch():
 global proc
 meta.unlink(missing_ok=True)
 proc=subprocess.Popen([str(exe),str(rom),'--paused','--frames','2','--capture',str(capture)])
 found=[]
 @cb
 def enum(hwnd,_):
  pid=W.DWORD();name=C.create_unicode_buffer(128)
  u.GetWindowThreadProcessId(hwnd,C.byref(pid));u.GetClassNameW(hwnd,name,len(name))
  if pid.value==proc.pid and name.value=='MatchaboyAutopsy':found.append(hwnd)
  return True
 def find():u.EnumWindows(enum,0);return found[0] if found else None
 hwnd=wait(find);wait(meta.exists);return hwnd,json.loads(meta.read_text())
def snap(hwnd):
 old=meta.stat().st_mtime_ns;send(hwnd,0x100,0x7B)
 wait(lambda:meta.stat().st_mtime_ns!=old)
 return json.loads(meta.read_text())
def close(hwnd):send(hwnd,0x10,0);assert proc.wait(timeout=10)==0
try:
 hwnd,initial=launch();reports.append(initial)
 for choice in [2,1,0]:
  send(hwnd,0x111,1030+choice);changed=snap(hwnd)
  assert changed['preference']==choice and changed['restart_required']
  raw=winreg.QueryValueEx(key,str(exe))[0]
  assert 'SwapEffectUpgradeEnable=0;' in raw
  assert (f'GpuPreference={choice};' in raw) if choice else ('GpuPreference=' not in raw)
  close(hwnd);hwnd,restarted=launch()
  assert restarted['preference']==choice and not restarted['restart_required']
  assert restarted['renderer']!='Unavailable'
  reports.append(restarted)
 close(hwnd)
 (out/'summary.json').write_text(json.dumps(dict(passed=True,runs=reports),indent=2));print(json.dumps(reports,indent=2))
finally:
 if proc and proc.poll() is None:proc.kill();proc.wait()
 winreg.DeleteValue(key,str(exe));key.Close()
