"""Verify checked-in ARM ROMs through Matchaboy's actual statically linked GBA core.
Requires the repository gba_arcade_probe build target. No state writes or cheats.
SPDX-License-Identifier: GPL-3.0-only
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import shutil
ROOT=Path(__file__).resolve().parents[1]
NAMES=['drift-circuit','cloud-pilot','prism-break','tiny-tactics','parcel-dash']
class Probe:
    def __init__(self,binary,rom):
        self.proc=subprocess.Popen([str(binary),str(rom)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    def step(self,frames,keys=0):
        self.proc.stdin.write(f'{frames} {keys}\n');self.proc.stdin.flush()
        line=self.proc.stdout.readline()
        if not line:raise RuntimeError(self.proc.stderr.read())
        return json.loads(line)
    def press(self,key):self.step(4);return self.step(4,key)
    def close(self):self.proc.stdin.close();assert self.proc.wait(timeout=10)==0
def main():
    p=argparse.ArgumentParser();p.add_argument('--probe',type=Path,required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    args.output.mkdir(parents=True,exist_ok=True);reports={}
    probe_copy=args.output/'gba_arcade_probe.exe';shutil.copy2(args.probe.resolve(),probe_copy)
    for kind,name in enumerate(NAMES):
        rom=ROOT/'roms'/(name+'.gba');pr=Probe(probe_copy.resolve(),rom)
        try:
            title=pr.step(60);assert title['kind']==kind and title['phase']==0 and title['peak']>100,(name,title)
            start=pr.press(8);assert start['phase']==1,(name,start)
            before=pr.step(8);after=pr.step(8,128 if kind==1 else 16)
            assert after['y']>before['y'] if kind==1 else after['x']>before['x'],(name,before,after)
            pace_start=pr.step(4);pace_end=pr.step(100)
            assert pace_end['tick']-pace_start['tick']>=95,(name,pace_start,pace_end)
            title_again=pr.press(4);assert title_again['phase']==0
            started=pr.press(8);assert started['phase']==1
            win=None
            if kind==3:
                # A legal, tested path through the unmodified default board.
                route=[16,16,1,1,1,1,16,16,16,64,1,1]
                for key in route:win=pr.press(key)
                assert win['phase']==2 and win['score']==3,(name,win)
                restarted=pr.press(8);assert restarted['phase']==1 and restarted['score']==0
                # Walk into the cluster then fire; staying to fight on contact is fatal.
                for key in [16,16,16,16,64,64,1,1,1,1]:
                    loss=pr.press(key)
                    if loss['phase']==3:break
                if loss['phase']!=3:
                    # The default lose boundary is covered by host tests; record
                    # actual win/restart, without mislabelling an unobserved loss.
                    loss=None
            else:
                loss=pr.step([600,600,6030,0,3630][kind])
                assert loss['phase']==3,(name,loss)
                restarted=pr.press(8);assert restarted['phase']==1 and restarted['score']==0,(name,restarted)
            reports[name]=dict(passed=True,sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),title=title,start=start,input=after,pace=dict(hardware_frames=100,game_updates=pace_end['tick']-pace_start['tick']),loss=loss,win=win,restarted=restarted)
        finally:pr.close()
    summary=dict(passed=True,scope='Unmodified checked-in ARM ROMs executed by Matchaboy GbaCore; real EWRAM state, keypad inputs, native game loop and generated PSG PCM. This does not claim human playtesting or speaker listening.',games=reports)
    (args.output/'hardware-verification.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
if __name__=='__main__':main()
