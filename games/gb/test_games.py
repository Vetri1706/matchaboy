#!/usr/bin/env python3
"""Replay full game objectives through the real Matchaboy SM83/PPU/input core.

SPDX-License-Identifier: GPL-3.0-only
Uses Python's standard library and the project's existing native test ABI only.
No game RAM is written, no program counters patched, and no emulated outcome is
substituted for the game's result. Failures are reached by playing badly/waiting.
The learning ABI omits PCM synthesis, so this suite does not establish speaker audio.
"""
from __future__ import annotations

import argparse
from collections import deque
import ctypes as C
import hashlib
import json
from pathlib import Path
import struct
import sys
import time
import zlib

from build_games import GAMES, GARDENS, MAZE, build

ROOT=Path(__file__).resolve().parent
MOVES=[(1,0,1),(-1,0,2),(0,-1,4),(0,1,8)]


def png(path, raw, width=160, height=144):
    """Write the real four-shade PPU output as a lossless native-resolution PNG."""
    levels=(248,168,80,16)
    rows=b''.join(b'\0'+bytes(levels[p] for p in raw[y*width:(y+1)*width]) for y in range(height))
    def chunk(kind,data):
        return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,0,0,0,0))+
                     chunk(b'IDAT',zlib.compress(rows,9))+chunk(b'IEND',b''))


class Machine:
    def __init__(self,path,library,output):
        self.path,self.output=path,output
        self.lib=C.CDLL(str(library))
        byteptr=C.POINTER(C.c_uint8)
        for name,restype,args in [
            ('gym_create',C.c_void_p,[C.c_char_p,C.c_int]),
            ('gym_destroy',None,[C.c_void_p]),
            ('gym_get_ram',None,[C.c_void_p,C.c_int,C.POINTER(byteptr)]),
            ('gym_step_checked',C.c_int,[C.c_void_p,byteptr,C.c_size_t,C.POINTER(C.c_float),byteptr]),
            ('gym_observation_ptr',C.c_int,[C.c_void_p,C.c_int,C.POINTER(byteptr),C.POINTER(C.c_size_t)]),
            ('gym_last_error',C.c_char_p,[])]:
            fn=getattr(self.lib,name); fn.restype=restype; fn.argtypes=args
        self.handle=self.lib.gym_create(str(path).encode(),1)
        if not self.handle: raise RuntimeError(self.lib.gym_last_error().decode())
        pointer=byteptr(); self.lib.gym_get_ram(self.handle,0,C.byref(pointer))
        self.ram=C.cast(pointer,C.POINTER(C.c_uint8*8192)).contents
        self.actions=(C.c_uint8*1)(); self.rewards=(C.c_float*1)(); self.dones=(C.c_uint8*1)()
        self.frames=0; self.input_digest=hashlib.sha256(); self.checks=[]

    def step(self,action=0,count=1):
        self.actions[0]=action
        for _ in range(count):
            result=self.lib.gym_step_checked(self.handle,self.actions,1,self.rewards,self.dones)
            if result: raise RuntimeError(self.lib.gym_last_error().decode())
            self.frames+=1; self.input_digest.update(bytes([action]))

    def tap(self,key):
        self.step(0,2); self.step(key,2); self.step(0,2)

    def check(self,condition,label):
        assert condition, f'{self.path.stem}: {label}; RAM={list(self.ram[:24])}'
        self.checks.append(label)

    def capture(self,name):
        # Scene changes copy the full tile map with LCD disabled; allow that copy
        # and a whole scanout to finish before taking an actual PPU screenshot.
        self.step(0,6)
        pointer=C.POINTER(C.c_uint8)(); length=C.c_size_t()
        result=self.lib.gym_observation_ptr(self.handle,0,C.byref(pointer),C.byref(length))
        assert not result and length.value==160*144
        pixels=bytes(pointer[:length.value])
        assert len(set(pixels))>=2
        png(self.output/f'{self.path.stem}-{name}.png',pixels)

    def start(self):
        self.step(0,20)
        self.check(self.ram[0]==0,'Boots into the title/instruction screen')
        self.capture('title')
        self.tap(128)
        self.check(self.ram[0]==1 and self.ram[5]==0,'Start begins a fresh playable round')
        self.capture('play')

    def restart(self):
        self.tap(128)
        self.check(self.ram[0]==1 and self.ram[5]==0 and self.ram[6]==3,'Start restarts and clears score/loss state')

    def close(self):
        if self.handle: self.lib.gym_destroy(self.handle); self.handle=None


def racer(m):
    x=m.ram[7]; m.tap(2)
    m.check(m.ram[7]==x-32,'Left changes exactly one lane')
    m.tap(1); m.check(m.ram[7]==x,'Right returns one lane')
    for _ in range(2800):
        if m.ram[0]!=1: break
        action=0
        if m.ram[7]==m.ram[9]: action=1 if m.ram[7]<108 else 2
        m.step(action)
    m.check(m.ram[0]==2 and m.ram[5]==20,'Avoiding rivals completes all 20 passes')
    m.capture('win'); m.restart()
    # Deliberately steer into each rival. This exercises collision, shields and loss.
    for _ in range(1000):
        if m.ram[0]!=1: break
        x,enemy=m.ram[7],m.ram[9]
        if x!=enemy: m.tap(1 if x<enemy else 2)
        else: m.step()
    m.check(m.ram[0]==3 and m.ram[6]==0,'Three actual collisions consume shields and end the round')
    m.capture('lose'); m.restart()


def courier(m):
    for _ in range(700):
        if m.ram[0]!=1: break
        x,y,left=m.ram[7],m.ram[8],m.ram[16]
        jump=(x==left-12 and y==112)
        m.step(1|(16 if jump else 0))
    m.check(m.ram[0]==2 and m.ram[5]==3 and m.ram[6]==3,'Jump, collect, and deliver all three parcels without losing a life')
    m.capture('win'); m.restart()
    for _ in range(700):
        if m.ram[0]!=1: break
        # Walk into the pit and stop before its far edge, without jumping.
        m.step(1 if m.ram[7]<m.ram[16]+4 else 0)
    m.check(m.ram[0]==3 and m.ram[6]==0,'Falling into three gaps consumes lives and ends the round')
    m.capture('lose'); m.restart()


def puzzle_solution(rows):
    walls=set(); crate=player=goal=None
    for y,row in enumerate(rows):
        for x,c in enumerate(row):
            if c=='#': walls.add((x,y))
            elif c=='$': crate=(x,y)
            elif c=='@': player=(x,y)
            elif c=='o': goal=(x,y)
    q=deque([(player,crate,[])]); seen={(player,crate)}
    while q:
        player,crate,path=q.popleft()
        if crate==goal: return path
        for dx,dy,key in MOVES:
            np=(player[0]+dx,player[1]+dy); nc=crate
            if np in walls: continue
            if np==crate:
                nc=(crate[0]+dx,crate[1]+dy)
                if nc in walls: continue
            state=(np,nc)
            if state not in seen:
                seen.add(state); q.append((np,nc,path+[key]))
    raise AssertionError('unsolvable garden')


def garden(m):
    initial=bytes(m.ram[0x200:0x600]); start=tuple(m.ram[7:9])
    m.tap(1); m.check(tuple(m.ram[7:9])!=start,'Arrows move the gardener on the grid')
    m.tap(32); m.check(tuple(m.ram[7:9])==start and bytes(m.ram[0x200:0x600])==initial,'B restores the current puzzle, including its mutable board')
    # Trying to walk out through the left boundary must not cross its wall.
    m.step(2,60); m.check(m.ram[7]==6,'Solid garden walls block player movement')
    m.tap(32)
    for level,rows in enumerate(GARDENS):
        for key in puzzle_solution(rows): m.tap(key)
        m.check(m.ram[5]==level+1,f'Actual pushes solve garden {level+1}')
        m.step(0,3)
        if level<2: m.capture(f'garden-{level+2}')
    m.check(m.ram[0]==2,'All three puzzles reach a completed round')
    m.capture('win'); m.restart()


def path_to(rows,start,target):
    q=deque([(start,[])]); seen={start}
    while q:
        current,path=q.popleft()
        if current==target: return path
        for dx,dy,key in MOVES:
            x,y=current[0]+dx,current[1]+dy
            if (x,y) not in seen and 0<=y<len(rows) and 0<=x<len(rows[y]) and rows[y][x]!='#':
                seen.add((x,y)); q.append(((x,y),path+[key]))
    raise AssertionError('unreachable maze objective')


def maze(m):
    start=tuple(m.ram[7:9]); m.tap(4)
    m.check(tuple(m.ram[7:9])==start,'Maze walls block movement')
    exit_cell=next((x,y) for y,row in enumerate(MAZE) for x,c in enumerate(row) if c=='E')
    def walk(target):
        position=(m.ram[7]-1,m.ram[8]-3)
        for key in path_to(MAZE,position,target): m.tap(key)
    walk(exit_cell)
    m.check(m.ram[0]==1,'Finding the exit before collecting all beacons does not win')
    for target in [(x,y) for y,row in enumerate(MAZE) for x,c in enumerate(row) if c=='b']:
        walk(target)
    m.check(m.ram[5]==3,'All three beacons can be reached and collected')
    m.capture('beacons')
    walk(exit_cell)
    m.check(m.ram[0]==2,'Three signals plus the exit complete the mission')
    m.capture('win'); m.restart()
    m.step(0,5500)
    m.check(m.ram[0]==3 and m.ram[19]==0,'The real 90-second battery timer can expire and end the round')
    m.capture('lose'); m.restart()


def orbit(m):
    x=m.ram[7]; m.step(1,8); m.check(m.ram[7]>x,'Player ship responds to held direction input')
    # Track the target, leading its horizontal velocity over the projectile's flight.
    for _ in range(7500):
        if m.ram[0]!=1: break
        x,enemy,y,direction=m.ram[7],m.ram[9],m.ram[10],m.ram[11]
        target=enemy+(1 if direction else -1)*((110-y)//8)
        target=max(8,min(144,target))
        action=16 | (1 if x<target-1 else 2 if x>target+1 else 0)
        m.step(action)
    m.check(m.ram[0]==2 and m.ram[5]==15,'Firing real projectiles destroys 15 drones and completes the round')
    m.capture('win'); m.restart()
    for _ in range(7500):
        if m.ram[0]!=1: break
        m.step()
    m.check(m.ram[0]==3 and m.ram[6]==0,'Three unopposed drone invasions consume lives and end the round')
    m.capture('lose'); m.restart()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library',type=Path,default=ROOT.parents[1]/'build/smooth/libmatcha.dll')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--game',choices=[g['id'] for g in GAMES])
    args=parser.parse_args(); args.output.mkdir(parents=True,exist_ok=True)
    library=args.library.resolve(); started=time.time(); results=[]
    for game in GAMES:
        if args.game and game['id']!=args.game: continue
        path=ROOT/'roms'/f'{game["id"]}.gb'
        expected,_,_=build(game)
        assert path.read_bytes()==expected,'ROM differs from its buildable source'
        m=Machine(path,library,args.output)
        try:
            m.start()
            {'pocket-racer':racer,'moon-courier':courier,'matcha-garden':garden,'signal-lost':maze,'orbit-guard':orbit}[game['id']](m)
            result={'id':game['id'],'passed':True,'checks':m.checks,'emulated_frames':m.frames,
                    'input_sha256':m.input_digest.hexdigest(),'rom_sha256':hashlib.sha256(expected).hexdigest()}
            results.append(result)
            print(f'PASS {game["id"]}: {len(m.checks)} checks, {m.frames} frames',flush=True)
        finally: m.close()
    summary={'suite':'Original GB arcade full input replays','library':str(library),
             'library_sha256':hashlib.sha256(library.read_bytes()).hexdigest(),
             'elapsed_seconds':round(time.time()-started,3),'ram_writes':False,'results':results,
             'limitations':['Automated input replays, not human playtesting.','Learning ABI does not synthesize speaker PCM.','Physical Game Boy hardware not tested.']}
    (args.output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps({'passed':len(results),'elapsed_seconds':summary['elapsed_seconds']}))


if __name__=='__main__': main()
