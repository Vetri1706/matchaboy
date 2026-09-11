"""Input-only playthrough policies against the actual unmodified GBA cartridges.
SPDX-License-Identifier: GPL-3.0-only
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
from test_hardware import Probe,ROOT,NAMES
def main():
    p=argparse.ArgumentParser();p.add_argument('--probe',type=Path,required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    args.output.mkdir(parents=True,exist_ok=True);probe=args.output/'play-probe.exe';shutil.copy2(args.probe,probe);reports={}
    for kind in [1,2,4,0]:
        name=NAMES[kind];rom=ROOT/'roms'/(name+'.gba');pr=Probe(probe.resolve(),rom)
        try:
            pr.step(60)
            if kind==0:
                s=pr.step(1,8)
                while s['phase']==0:s=pr.step(1,8)
                assert s['tick']==0,s
                route=json.loads((ROOT/'tests/drift-winning-inputs.json').read_text())
                for key in route['steps']:
                    s=pr.step(route['frames_per_step'],key)
                    if s['phase']!=1:break
                reports[name]=dict(won=s['phase']==2,end=s,sha256=hashlib.sha256(rom.read_bytes()).hexdigest())
                print(name,s['phase'],s['score'],s['hp'],s['time'],flush=True)
                continue
            s=pr.press(8);s=pr.step(4)
            waypoints=[];delivery_state=None
            for frame in range(6200):
                if s['phase']!=1:break
                if kind==1:
                    near=min((o for o in s['objects'] if o['x']>=s['x']-13),key=lambda o:o['x'])
                    keys=128 if s['y']<near['y'] else 64 if s['y']>near['y'] else 0
                elif kind==2:
                    bx=s['objects'][0]['x'];by=s['objects'][0]['y']
                    target=max(24,min(216,bx+(14 if (s['score']//3)%2 else -14)))
                    keys=(16 if s['x']<target-1 else 32 if s['x']>target+1 else 0)
                    if not s['carrying']:keys|=1
                elif kind==4:
                    # Use the clear horizontal streets at y=60 and y=132.
                    tx=[204,36,204,120,36][s['target']%5] if s['carrying'] else 36
                    ty=[112,48,48,128,112][s['target']%5] if s['carrying'] else 80
                    if abs(s['x']-tx)<2 and abs(s['y']-ty)<2:
                        s=pr.press(1);continue
                    state=(s['carrying'],s['target'])
                    if state!=delivery_state:
                        corridor=132 if (s['x']==120 or tx==120) else 60
                        waypoints=[(s['x'],corridor),(tx,corridor),(tx,ty)];delivery_state=state
                    while waypoints and (s['x'],s['y'])==waypoints[0]:waypoints.pop(0)
                    wx,wy=waypoints[0]
                    keys=(16 if s['x']<wx else 32) if s['x']!=wx else (128 if s['y']<wy else 64)
                s=pr.step(1,keys)
            reports[name]=dict(won=s['phase']==2,end=s,sha256=hashlib.sha256(rom.read_bytes()).hexdigest())
            print(name,s['phase'],s['score'],s['hp'],s['time'],flush=True)
        finally:pr.close()
    result=dict(scope='Actual ARM ROM playthroughs using only GBA keypad inputs; no memory writes or ROM modifications.',games=reports)
    (args.output/'playthroughs.json').write_text(json.dumps(result,indent=2)+'\n')
    assert all(r['won'] for r in reports.values()),reports
if __name__=='__main__':main()
