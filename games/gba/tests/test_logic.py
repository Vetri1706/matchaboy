"""Exercise the exact ROM game logic natively, including success/failure/restart.
SPDX-License-Identifier: GPL-3.0-only
"""
import argparse
import ctypes as C
import json
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from build import find

class Obj(C.Structure):
    _fields_=[(n,C.c_int) for n in ('x','y','vx','vy','alive','hp')]
class Game(C.Structure):
    _fields_=[(n,C.c_int) for n in ('kind','phase','frame','tick','score','time','hp','x','y','vx','vy','prev','pressed','sound','cooldown','stage','target','carrying','moves')]+[('bricks',C.c_int*24),('obj',Obj*12)]
A,B,SELECT,START,RIGHT,LEFT,UP,DOWN=1,2,4,8,16,32,64,128
def main():
    p=argparse.ArgumentParser();p.add_argument('--llvm-dir',type=Path,default=Path(r'C:\Program Files\LLVM\bin'));args=p.parse_args()
    out=ROOT/'build';out.mkdir(exist_ok=True)
    clang=find('clang',args.llvm_dir);link=find('lld-link',args.llvm_dir)
    obj=out/'host.obj';dll=out/'game-logic.dll'
    subprocess.run([clang,'--target=x86_64-pc-windows-msvc','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-DHOST_TEST','-c',str(ROOT/'src/game.c'),'-o',str(obj)],check=True)
    subprocess.run([link,'/dll','/noentry','/nodefaultlib','/out:'+str(dll),str(obj)],check=True)
    lib=C.CDLL(str(dll));lib.game_init.argtypes=[C.POINTER(Game),C.c_int];lib.game_step.argtypes=[C.POINTER(Game),C.c_int]
    def init(kind):
        g=Game();lib.game_init(C.byref(g),kind);assert g.phase==0
        step(g,START);assert g.phase==1;step(g,0);return g
    def step(g,keys=0,count=1):
        for _ in range(count):lib.game_step(C.byref(g),keys)
    def press(g,k):step(g,0);step(g,k)
    def restart(g):
        assert g.phase in (2,3);press(g,START);assert g.phase==1 and g.score==0 and g.tick==0
    checks=[]
    # Input and genuine timed end states, rather than an implementation-only snapshot.
    for kind in range(5):
        g=init(kind);x,y=g.x,g.y;press(g,DOWN if kind==1 else RIGHT)
        assert (g.y>y if kind==1 else g.x>x),(kind,x,g.x)
        if kind!=3:
            g.time=1;step(g);assert g.phase==3;restart(g)
        step(g,SELECT);assert g.phase==0
    checks.append('All five accept Start, movement, Select/title and timed round restart (tactics has turn limits instead).')
    # Complete the driving course with an automated steering policy; clear traffic
    # only for this focused distance/timer check, collisions are tested separately.
    g=init(0)
    for o in g.obj:o.y=-100000
    for _ in range(1300):
        center=lib.road_center(g.stage+3);step(g,A|(RIGHT if g.x<center else LEFT if g.x>center else 0))
        if g.phase!=1:break
    assert g.phase==2 and g.stage>=3600;restart(g)
    g.obj[0].x=g.x;g.obj[0].y=g.y-2;g.hp=1;step(g);assert g.phase==3;restart(g)
    checks.append('Drift course finish, steering, boost, traffic collision loss and restart.')
    g=init(1)
    for _ in range(2500):
        ahead=[o for o in g.obj[:4] if o.x>=g.x-13]
        nearest=min(ahead,key=lambda o:o.x) if ahead else g.obj[0]
        step(g,(DOWN if g.y<nearest.y else UP if g.y>nearest.y else 0))
        if g.phase!=1:break
    assert g.phase==2 and g.score>=24,(g.phase,g.score,g.hp);restart(g)
    g.hp=1;g.obj[0].x=g.x+2;g.obj[0].y=30;g.y=100;step(g);assert g.phase==3;restart(g)
    checks.append('Cloud automated full 24-gate flight, moving gap collision loss and restart.')
    g=init(2);press(g,A);assert g.carrying
    initial_y=g.obj[0].y;step(g,0,8);assert g.obj[0].y<initial_y
    # Meaningful boundary fixtures: paddle bounce, final brick, last missed ball.
    g.obj[0].x=g.x;g.obj[0].y=118;g.obj[0].vy=2;step(g);assert g.obj[0].vy<0
    for i in range(24):g.bricks[i]=0
    g.bricks[0]=1;g.score=23;g.obj[0].x=18;g.obj[0].y=41;g.obj[0].vx=0;g.obj[0].vy=-2;step(g);assert g.phase==2;restart(g)
    g.carrying=1;g.obj[0].y=145;g.obj[0].vy=2;g.hp=1;step(g);assert g.phase==3;restart(g)
    checks.append('Prism launch, moving ball, paddle collision, final brick win, lost ball and restart.')
    g=init(3)
    # Exhaustive bounded search finds an actual winning sequence on the default board.
    seen=set();route=[]
    def solve(state,path):
        if state.phase==2:return path
        if state.phase!=1 or len(path)>17:return None
        sig=(state.x,state.y,state.hp,state.target,tuple((o.x,o.y,o.hp,o.alive) for o in state.obj[:3]))
        if sig in seen:return None
        seen.add(sig)
        for key in [A,RIGHT,UP,DOWN,LEFT,B]:
            child=Game.from_buffer_copy(state);before=child.moves;press(child,key)
            if child.moves==before and key!=B and child.phase==1:continue
            found=solve(child,path+[key])
            if found:return found
        return None
    route=solve(g,[]);assert route,'Default tactical board must be winnable'
    for key in route:press(g,key)
    assert g.phase==2 and g.score==3;restart(g)
    g.hp=1;g.obj[0].x=1;g.obj[0].y=4;g.obj[0].hp=2;press(g,A);assert g.phase==3;restart(g)
    checks.append('Tactics default board has a verified legal winning route; enemy retaliation loss and restart.')
    g=init(4);press(g,A);assert g.carrying
    for o in g.obj:o.y=-1000
    for parcel in range(5):
        if not g.carrying:
            while g.x!=36 or g.y!=80:
                step(g,RIGHT if g.x<36 else LEFT if g.x>36 else DOWN if g.y<80 else UP)
            press(g,A)
        tx=lib.parcel_target_x(g.target);ty=lib.parcel_target_y(g.target)
        while g.x!=tx or g.y!=ty:step(g,RIGHT if g.x<tx else LEFT if g.x>tx else DOWN if g.y<ty else UP)
        press(g,A)
    assert g.phase==2 and g.score==5;restart(g)
    g.hp=1;g.obj[0].x=g.x-1;g.obj[0].y=g.y;step(g);assert g.phase==3;restart(g)
    checks.append('Parcel five pickup/travel/delivery loops, traffic collision loss and restart.')
    report=dict(passed=True,scope='Exact ROM game.c compiled to a native test DLL; hardware/UI verification is separate.',checks=checks,tactics_winning_keys=route)
    (out/'logic-verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
