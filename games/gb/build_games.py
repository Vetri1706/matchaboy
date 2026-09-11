#!/usr/bin/env python3
"""Build five original, stand-alone Game Boy games. SPDX-License-Identifier: GPL-3.0-only.

The small SM83 assembler, game logic, levels, tile art, lettering and sound sequences
were authored by the coding assistant in Codex under the project owner's direction.
No external game engine, asset pack, assembler or runtime is used. Python is a
development tool only; each result is a 32 KiB ROM-only cartridge.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent

# State in ordinary cartridge WRAM, exposed here for learning and reproducible tests.
STATE, TICK, PREVIOUS, KEYS, EDGE = range(0xC000, 0xC005)
SCORE, LIVES, PX, PY, EX, EY, DIRECTION, TIMER, AUX, JUMP, BULLET = range(0xC005, 0xC010)
BX, BY, STEP_CLOCK, SECONDS, SUBSECOND, LEVEL, CARRY, TEMP = range(0xC010, 0xC018)
BOARD = 0xC200
RIGHT, LEFT, UP, DOWN, A_BUTTON, B_BUTTON, SELECT, START = (1, 2, 4, 8, 16, 32, 64, 128)


class Assembler:
    """Only the documented instructions needed by these games; no hidden runtime."""
    def __init__(self):
        self.code = bytearray()
        self.labels: dict[str, int] = {}
        self.fixups: list[tuple[int, str]] = []
        self.serial = 0

    def emit(self, *values):
        self.code.extend(v & 255 for v in values)

    def label(self, name):
        if name in self.labels:
            raise ValueError(f"duplicate label: {name}")
        self.labels[name] = 0x150 + len(self.code)

    def unique(self, stem="branch"):
        self.serial += 1
        return f"{stem}_{self.serial}"

    def word(self, address):
        if isinstance(address, str):
            self.fixups.append((len(self.code), address))
            self.emit(0, 0)
        else:
            self.emit(address, address >> 8)

    def jp(self, label, condition=None):
        self.emit({None: 0xC3, "nz": 0xC2, "z": 0xCA, "nc": 0xD2, "c": 0xDA}[condition])
        self.word(label)

    def call(self, label):
        self.emit(0xCD)
        self.word(label)

    def ret(self):
        self.emit(0xC9)

    def pair(self, register, value):
        self.emit({"bc": 1, "de": 0x11, "hl": 0x21, "sp": 0x31}[register])
        self.word(value)

    def load(self, address):
        self.emit(0xFA)
        self.word(address)

    def store(self, address):
        self.emit(0xEA)
        self.word(address)

    def value(self, address, value):
        self.emit(0x3E, value)
        self.store(address)

    def inc(self, address):
        self.pair("hl", address)
        self.emit(0x34)

    def dec(self, address):
        self.pair("hl", address)
        self.emit(0x35)

    def io(self, register, value):
        self.emit(0x3E, value, 0xE0, register & 255)

    def test_key(self, mask, target, held=False):
        self.load(KEYS if held else EDGE)
        self.emit(0xE6, mask)
        self.jp(target, "z")

    def finish(self):
        for offset, label in self.fixups:
            self.code[offset:offset + 2] = self.labels[label].to_bytes(2, "little")
        return bytes(self.code)


# Original five-column lettering, drawn explicitly rather than taken from a font file.
FONT = {
    "A": [14,17,17,31,17,17,17], "B": [30,17,17,30,17,17,30],
    "C": [14,17,16,16,16,17,14], "D": [30,17,17,17,17,17,30],
    "E": [31,16,16,30,16,16,31], "F": [31,16,16,30,16,16,16],
    "G": [14,17,16,23,17,17,15], "H": [17,17,17,31,17,17,17],
    "I": [14,4,4,4,4,4,14], "J": [7,2,2,2,18,18,12],
    "K": [17,18,20,24,20,18,17], "L": [16,16,16,16,16,16,31],
    "M": [17,27,21,21,17,17,17], "N": [17,25,25,21,19,19,17],
    "O": [14,17,17,17,17,17,14], "P": [30,17,17,30,16,16,16],
    "Q": [14,17,17,17,21,18,13], "R": [30,17,17,30,20,18,17],
    "S": [15,16,16,14,1,1,30], "T": [31,4,4,4,4,4,4],
    "U": [17,17,17,17,17,17,14], "V": [17,17,17,17,17,10,4],
    "W": [17,17,17,21,21,21,10], "X": [17,17,10,4,10,17,17],
    "Y": [17,17,10,4,4,4,4], "Z": [31,1,2,4,8,16,31],
    "0": [14,17,19,21,25,17,14], "1": [4,12,4,4,4,4,14],
    "2": [14,17,1,2,4,8,31], "3": [30,1,1,14,1,1,30],
    "4": [2,6,10,18,31,2,2], "5": [31,16,16,30,1,1,30],
    "6": [14,16,16,30,17,17,14], "7": [31,1,2,4,8,8,8],
    "8": [14,17,17,14,17,17,14], "9": [14,17,17,15,1,1,14],
    ":": [0,4,4,0,4,4,0], "/": [1,2,2,4,8,8,16],
    "-": [0,0,0,31,0,0,0], "!": [4,4,4,4,4,0,4],
    ".": [0,0,0,0,0,4,4], "+": [0,4,4,31,4,4,0],
    "?": [14,17,1,2,4,0,4], "<": [2,4,8,16,8,4,2],
    ">": [8,4,2,1,2,4,8], "=": [0,31,0,31,0,0,0],
}


def art(*rows):
    assert len(rows) == 8 and all(len(row) == 8 for row in rows)
    result = bytearray()
    for row in rows:
        colors = [int(c) for c in row]
        result.extend((sum((c & 1) << (7-i) for i,c in enumerate(colors)),
                       sum(((c >> 1) & 1) << (7-i) for i,c in enumerate(colors))))
    return result


def tiles():
    data = bytearray(160 * 16)
    patterns = {
        1: ["22222222","23333332","23000032","23000032","23000032","23000032","23333332","22222222"],
        2: ["33333333","22222222","01010101","10101010","00020000","20000020","00100000","10000100"],
        3: ["11111111"] * 8,
        4: ["11033111"] * 6 + ["11111111"] * 2,
        5: ["00010000","00131000","01333100","00131000","00030000","03030300","00333000","00030000"],
        6: ["03333330","03222230","03233230","03322330","03233230","03222230","03333330","00000000"],
        7: ["00030000","00333000","03000300","00333000","00030000","00333000","03000300","03333300"],
        8: ["33333333","30000003","30222203","30200203","30200203","30200203","30200203","33333333"],
        9: ["00000000","00010000","00030000","01333100","00030000","00010000","00000000","00000000"],
        10: ["00022000","00211200","02211120","21111112","21121112","02111120","00222200","00000000"],
        11: ["00000000","12121212","21212121","11111111","12121212","21212121","11111111","11111111"],
        12: ["33333333","30000003","30200203","30022003","30022003","30200203","30000003","33333333"],
        128: ["00333300","03111130","33222233","33111133","03111130","33111133","33222233","00333300"], # car
        129: ["00333300","03322330","03222230","03333330","03222230","03322330","00333300","00000000"], # rival
        130: ["00333300","03222300","03232300","03333300","00333000","03333330","00300300","03300330"], # courier
        131: ["00333300","03333330","03232330","03333330","00333000","03333330","03033030","00033000"], # gardener
        132: ["00033000","00333300","03233230","33333333","03333330","00333300","00300300","03000030"], # explorer
        133: ["00033000","00033000","00333300","00311300","03311330","33311333","33033033","00022000"], # ship
        134: ["03300330","00333300","03311330","33133133","33333333","03033030","30000003","00000000"], # invader
        135: ["00033000","00033000","00033000","00033000","00011000","00000000","00000000","00000000"],
        136: ["00011000","00133100","01333310","13333331","01333310","00133100","00011000","00000000"],
    }
    for number, rows in patterns.items():
        data[number * 16:(number + 1)*16] = art(*rows)
    for char, rows in FONT.items():
        for y, bits in enumerate([0] + rows):
            data[ord(char)*16+y*2:ord(char)*16+y*2+2] = bytes([bits << 2, bits << 2])
    return bytes(data)


def screen():
    return bytearray(1024)


def text(m, y, message, x=None):
    assert len(message) <= 20, message
    x = (20-len(message))//2 if x is None else x
    m[y*32+x:y*32+x+len(message)] = message.encode("ascii")


def frame(m, left, top, right, bottom, tile=1):
    for x in range(left, right+1):
        m[top*32+x] = m[bottom*32+x] = tile
    for y in range(top, bottom+1):
        m[y*32+left] = m[y*32+right] = tile


GAMES = [
    dict(id="pocket-racer", title="Pocket Racer", goal="Pass 20 rival cars before your three shields are gone.",
         controls="Left/Right: change lane. Enter (Start) or Z (A): start/restart.",
         learn="A scrolling road, lane decisions and rectangle collision.",
         inspector_hint="Memory highlights activity at C005 (passes), C006 (shields) and C007/C009 (car X). Video shows object-fetch activity for the cars.",
         lines=["THREE LANES. GO!", "LEFT / RIGHT: STEER", "PASS 20 RIVAL CARS", "THREE SHIELDS"], icon=128),
    dict(id="moon-courier", title="Moon Courier", goal="Pick up a parcel and reach the moon base on each of three routes.",
         controls="Left/Right: walk. Z (A): jump. Enter (Start) or Z: start/restart.",
         learn="Signed vertical velocity, gravity, ground checks, parcels and delivery state.",
         inspector_hint="Memory shows activity at C007/C008 (position), C00E (jump velocity), C016 (parcel) and C005 (deliveries). Audio shows the jump-note envelope.",
         lines=["LOW GRAVITY. BIG JOB", "LEFT / RIGHT: WALK", "A: JUMP OVER GAPS", "GET BOX. REACH BASE."], icon=130),
    dict(id="matcha-garden", title="Matcha Garden", goal="Push the tea crate onto the flower in three compact garden puzzles.",
         controls="Arrows: move/push. X (B): reset this puzzle. Enter (Start) or Z (A): start/restart.",
         learn="Grid movement, checking the next two cells, and a mutable tile board.",
         inspector_hint="Memory highlights board activity beginning at C200, with 32 bytes per row. Moving a crate writes both the board and the background tile map at 9800.",
         lines=["THREE TEA GARDENS", "ARROWS: MOVE + PUSH", "BOX ON FLOWER = WIN", "B: RESET A GARDEN"], icon=131),
    dict(id="signal-lost", title="Signal Lost", goal="Collect all three beacons, then find the exit before the 90-second battery runs out.",
         controls="Arrows: explore. Enter (Start) or Z (A): start/restart.",
         learn="A connected maze, collectible state, timed objectives and input repeat.",
         inspector_hint="Watch memory activity at C005 (signals), C013 (battery), and the maze at C200. Collecting a beacon writes its empty tile to the background map.",
         lines=["FIND THREE BEACONS", "ARROWS: EXPLORE", "THEN FIND THE EXIT", "90 SECOND BATTERY"], icon=132),
    dict(id="orbit-guard", title="Orbit Guard", goal="Defeat 15 approaching drones before three reach your orbit.",
         controls="Left/Right: move. Hold Z (A): fire. Enter (Start) or Z: start/restart.",
         learn="Independent player, projectile and enemy motion with AABB hits and enemy waves.",
         inspector_hint="Memory shows drone activity at C009/C00A and projectile activity at C010/C011. Audio shows real pulse firing tones and noise impact envelopes.",
         lines=["DEFEND THE TEA MOON", "LEFT / RIGHT: MOVE", "HOLD A: FIRE PULSES", "15 DRONES. 3 LIVES."], icon=133),
]


def title_map(game):
    m = screen()
    frame(m, 0, 0, 19, 17)
    text(m, 2, "MATCHABOY ORIGINALS")
    text(m, 4, game["title"].upper())
    for x in (6, 9, 12):
        m[6*32+x] = game["icon"]
    for y, line in zip((8, 10, 12, 14), game["lines"]):
        text(m, y, line)
    text(m, 16, "START / A TO PLAY")
    return m


def ending_map(game, won):
    m = screen()
    frame(m, 0, 0, 19, 17)
    text(m, 2, "MATCHABOY ORIGINALS")
    text(m, 4, game["title"].upper())
    text(m, 7, "MISSION COMPLETE!" if won else "ROUND OVER")
    text(m, 9, "A LITTLE TEA BREAK." if won else "ONE MORE TRY?")
    for x in range(4, 17, 3):
        m[11*32+x] = 5 if won else 9
    text(m, 13, "START / A: RESTART")
    text(m, 15, "BUILT IN CODEX")
    return m


def load_map(a, label):
    a.pair("de", label)
    a.call("show_map")


def shared(a, game):
    a.label("boot")
    a.emit(0xF3)
    a.pair("sp", 0xDFFE)
    a.io(0x40, 0)
    a.pair("hl", 0x8000)
    a.pair("bc", 0x2000)
    a.call("clear")
    a.pair("hl", 0xC000)
    a.pair("bc", 0x1800)
    a.call("clear")
    a.pair("de", "tiles")
    a.pair("hl", 0x8000)
    a.pair("bc", 160*16)
    a.call("copy")
    a.io(0x47, 0xE4)
    a.io(0x48, 0xE4)
    a.io(0x49, 0xE4)
    a.io(0x42, 0)
    a.io(0x43, 0)
    a.io(0x26, 0x80)
    a.io(0x24, 0x77)
    a.io(0x25, 0xFF)
    load_map(a, "title_map")
    a.label("main")
    # Leave the current VBlank, then wait for the next: exactly one game tick per frame.
    a.label("wait_visible")
    a.emit(0xF0, 0x44, 0xFE, 144)
    a.jp("wait_visible", "nc")
    a.label("wait_vblank")
    a.emit(0xF0, 0x44, 0xFE, 144)
    a.jp("wait_vblank", "c")
    a.call("input")
    a.inc(TICK)
    a.load(STATE)
    a.emit(0xFE, 1)
    a.jp("playing", "z")
    a.test_key(START | A_BUTTON, "main")
    a.call("new_game")
    a.jp("main")
    a.label("playing")
    a.load(TICK)
    a.emit(0xE6, 31)
    a.jp("no_ambient", "nz")
    a.call("ambient")
    a.label("no_ambient")
    a.call("game_step")
    a.load(STATE)
    a.emit(0xFE, 1)
    a.jp("main", "nz")
    a.call("draw")
    a.jp("main")

    a.label("clear")
    a.emit(0xAF, 0x22, 0x0B, 0x78, 0xB1)
    a.jp("clear", "nz")
    a.ret()
    a.label("copy")
    a.emit(0x1A, 0x22, 0x13, 0x0B, 0x78, 0xB1)
    a.jp("copy", "nz")
    a.ret()
    a.label("show_map")
    a.io(0x40, 0)
    a.pair("hl", 0x9800)
    a.pair("bc", 1024)
    a.call("copy")
    a.pair("hl", 0xFE00)
    a.pair("bc", 160)
    a.call("clear")
    a.io(0x40, 0x93)
    a.ret()
    a.label("input")
    a.io(0x00, 0x20)
    a.emit(0xF0, 0, 0xF0, 0, 0xF0, 0, 0x2F, 0xE6, 15, 0x47)
    a.io(0x00, 0x10)
    a.emit(0xF0, 0, 0xF0, 0, 0xF0, 0, 0x2F, 0xE6, 15, 0xCB, 0x37, 0xB0)
    a.store(KEYS)
    a.emit(0x47)
    a.load(PREVIOUS)
    a.emit(0x2F, 0xA0)
    a.store(EDGE)
    a.emit(0x78)
    a.store(PREVIOUS)
    a.io(0x00, 0x30)
    a.ret()
    a.label("new_game")
    a.pair("hl", SCORE)
    a.pair("bc", 27)
    a.call("clear")
    a.value(STATE, 1)
    a.value(LIVES, 3)
    a.value(SECONDS, 90)
    a.call("initialize")
    a.call("good")
    a.ret()
    for name, state, mapname in [("win", 2, "win_map"), ("lose", 3, "lose_map")]:
        a.label(name)
        a.value(STATE, state)
        load_map(a, mapname)
        a.call("good" if state == 2 else "crash")
        a.ret()
    # BC register pair is intentionally caller-saved, as are all others.
    a.label("number")  # A:0..99; HL destination, writes two numeric font tiles.
    a.emit(0x06, 48)
    a.label("tens")
    a.emit(0xFE, 10)
    a.jp("ones", "c")
    a.emit(0xD6, 10, 0x04)
    a.jp("tens")
    a.label("ones")
    a.emit(0xC6, 48, 0x4F, 0x78, 0x22, 0x71)
    a.ret()
    a.label("grid_address")  # B:x, C:y -> HL=$C200+y*32+x. 0..31 coordinates.
    a.emit(0x69, 0x26, 0, 0x29, 0x29, 0x29, 0x29, 0x29, 0x58, 0x16, 0, 0x19)
    a.pair("de", BOARD)
    a.emit(0x19)
    a.ret()
    a.label("paint_cell")  # HL is board cell. Write A to RAM and its VRAM tile.
    a.emit(0x77, 0xE5)
    a.pair("de", 0x9800 - BOARD & 65535)
    a.emit(0x19, 0x77, 0xE1)
    a.ret()
    # Envelope-driven short notes; no sampled commercial sounds.
    for name, frequency, high, volume in [("good", 0xC0, 0x87, 0x82), ("jump_sound", 0x40, 0x87, 0x72), ("fire", 0x80, 0x86, 0x62), ("move_sound", 0, 0x87, 0x32)]:
        a.label(name)
        for r,v in [(0x10,0),(0x11,0xA0),(0x12,volume),(0x13,frequency),(0x14,high|0x40)]:
            a.io(r,v)
        a.ret()
    a.label("ambient")
    for r,v in [(0x16,0x68),(0x17,0x21)]: a.io(r,v)
    a.load(TICK)
    a.emit(0xE6, 0x60, 0xC6, 0x80, 0xE0, 0x18)
    a.io(0x19, 0xC6)
    a.ret()
    a.label("crash")
    for r,v in [(0x20,0x20),(0x21,0x91),(0x22,0x35),(0x23,0xC0)]: a.io(r,v)
    a.ret()


def sprite(a, index, xvar, yvar, tile, xoffset=8, yoffset=16):
    a.load(yvar)
    a.emit(0xC6, yoffset)
    a.store(0xFE00+index*4)
    a.load(xvar)
    a.emit(0xC6, xoffset)
    a.store(0xFE01+index*4)
    a.value(0xFE02+index*4, tile)
    a.value(0xFE03+index*4, 0)


def hud(a, left="SCORE", right="LIVES"):
    a.load(SCORE)
    a.pair("hl", 0x9800+len(left)+1)
    a.call("number")
    a.load(LIVES)
    a.emit(0xC6, 48)
    a.store(0x9800+19)


def horizontal(a, step=2, minimum=8, maximum=144):
    skip = a.unique("right")
    a.test_key(LEFT, skip, held=True)
    a.load(PX)
    a.emit(0xFE, minimum+step)
    a.jp(skip, "c")
    a.emit(0xD6, step)
    a.store(PX)
    a.label(skip)
    skip = a.unique("x_done")
    a.test_key(RIGHT, skip, held=True)
    a.load(PX)
    a.emit(0xFE, maximum-step+1)
    a.jp(skip, "nc")
    a.emit(0xC6, step)
    a.store(PX)
    a.label(skip)


def racer(a):
    m = screen()
    for y in range(2, 17):
        for x in range(4, 16): m[y*32+x] = 4 if x in (8,12) and y%3 != 0 else 3
        m[y*32+3] = m[y*32+16] = 2
    text(m,0,"PASS 00/20  SHIELD 3",0)
    text(m,17,"LEFT / RIGHT",4)
    a.label("initialize")
    load_map(a,"game_map")
    a.value(PX,76); a.value(PY,112); a.value(EY,20); a.value(EX,44)
    a.ret()
    a.label("game_step")
    a.test_key(LEFT,"racer_right")
    a.load(PX); a.emit(0xFE,44); a.jp("racer_right","z")
    a.emit(0xD6,32); a.store(PX); a.call("move_sound")
    a.label("racer_right")
    a.test_key(RIGHT,"racer_movement")
    a.load(PX); a.emit(0xFE,108); a.jp("racer_movement","z")
    a.emit(0xC6,32); a.store(PX); a.call("move_sound")
    a.label("racer_movement")
    a.load(TICK); a.emit(0xE6,1); a.jp("racer_done","nz")
    a.inc(EY); a.inc(EY)
    a.load(EY); a.emit(0xFE,106); a.jp("racer_done","c")
    a.emit(0xFE,120); a.jp("racer_pass","nc")
    a.load(EX); a.emit(0x47); a.load(PX); a.emit(0xB8); a.jp("racer_done","nz")
    a.dec(LIVES); a.call("crash"); a.load(LIVES); a.emit(0xB7); a.jp("lose","z")
    a.jp("racer_spawn")
    a.label("racer_pass")
    a.inc(SCORE); a.call("good"); a.load(SCORE); a.emit(0xFE,20); a.jp("win","nc")
    a.label("racer_spawn")
    a.value(EY,20)
    a.inc(AUX); a.load(AUX); a.emit(0xFE,3); a.jp("racer_pattern","c")
    a.value(AUX,0)
    a.label("racer_pattern")
    a.load(AUX); a.emit(0x87,0x87,0x87,0x87,0x87,0xC6,44); a.store(EX)
    a.label("racer_done"); a.ret()
    a.label("draw")
    sprite(a,0,PX,PY,128); sprite(a,1,EX,EY,129)
    a.load(SCORE); a.pair("hl",0x9805); a.call("number")
    a.load(LIVES); a.emit(0xC6,48); a.store(0x9813)
    a.ret()
    return {"game_map": m}


def courier(a):
    maps = {}
    for level, (gap1, gap2) in enumerate([(7,10),(9,12),(6,10)]):
        m=screen()
        text(m,0,"POST 00/03   LIVES 3",0)
        for x,y in [(2,3),(7,4),(15,2),(18,5),(4,7),(11,6)]: m[y*32+x]=9
        m[4*32+3]=10; m[14*32+14]=6; m[14*32+18]=8
        for y in (15,16):
            for x in range(20): m[y*32+x]=11 if gap1<=x<gap2 else 2
        text(m,17,"A: JUMP  >: DELIVER",0)
        maps[f"route_{level}"]=m
    a.label("initialize"); a.jp("route_start")
    a.label("route_start")
    a.load(SCORE); a.emit(0xB7); a.jp("route_first","z")
    a.emit(0xFE,1); a.jp("route_second","z")
    load_map(a,"route_2"); a.value(BX,48); a.value(BY,80); a.jp("route_positions")
    a.label("route_first"); load_map(a,"route_0"); a.value(BX,56); a.value(BY,80); a.jp("route_positions")
    a.label("route_second"); load_map(a,"route_1"); a.value(BX,72); a.value(BY,96)
    a.label("route_positions")
    a.value(PX,16); a.value(PY,112); a.value(JUMP,0); a.value(CARRY,0); a.value(AUX,0)
    a.ret()
    a.label("game_step")
    horizontal(a,step=1,maximum=148)
    a.test_key(A_BUTTON,"courier_gravity")
    a.load(PY); a.emit(0xFE,112); a.jp("courier_gravity","nz")
    a.load(JUMP); a.emit(0xB7); a.jp("courier_gravity","nz")
    a.value(JUMP,250); a.call("jump_sound")
    a.label("courier_gravity")
    a.inc(AUX); a.load(AUX); a.emit(0xFE,4); a.jp("courier_height","c")
    a.value(AUX,0); a.load(JUMP); a.emit(0xFE,5); a.jp("courier_height","z")
    a.inc(JUMP)
    a.label("courier_height")
    a.load(JUMP); a.emit(0x47); a.load(PY); a.emit(0x80); a.store(PY)
    a.load(JUMP); a.emit(0xCB,0x7F); a.jp("courier_parcel","nz") # upward velocity
    a.load(PY); a.emit(0xFE,112); a.jp("courier_parcel","c")
    a.load(BX); a.emit(0x47); a.load(PX); a.emit(0xC6,4,0xB8); a.jp("courier_land","c")
    a.load(BY); a.emit(0x47); a.load(PX); a.emit(0xC6,4,0xB8); a.jp("courier_land","nc")
    a.load(PY); a.emit(0xFE,139); a.jp("courier_parcel","c")
    a.dec(LIVES); a.call("crash"); a.load(LIVES); a.emit(0xB7); a.jp("lose","z")
    a.jp("route_start")
    a.label("courier_land"); a.value(PY,112); a.value(JUMP,0); a.value(AUX,0)
    a.label("courier_parcel")
    a.load(PX); a.emit(0xFE,106); a.jp("courier_done","c")
    a.emit(0xFE,121); a.jp("courier_delivery","nc")
    a.load(PY); a.emit(0xFE,100); a.jp("courier_done","c")
    a.load(CARRY); a.emit(0xB7); a.jp("courier_delivery","nz")
    a.value(CARRY,1); a.value(0x9800+14*32+14,0); a.call("good")
    a.label("courier_delivery")
    a.load(PX); a.emit(0xFE,144); a.jp("courier_done","c")
    a.load(PY); a.emit(0xFE,104); a.jp("courier_done","c")
    a.load(CARRY); a.emit(0xB7); a.jp("courier_done","z")
    a.inc(SCORE); a.call("good"); a.load(SCORE); a.emit(0xFE,3); a.jp("win","z")
    a.jp("route_start")
    a.label("courier_done"); a.ret()
    a.label("draw"); sprite(a,0,PX,PY,130)
    a.load(SCORE); a.pair("hl",0x9805); a.call("number")
    a.load(LIVES); a.emit(0xC6,48); a.store(0x9813)
    a.ret()
    return maps


GARDENS = [
    ["##########", "#........#", "#..$...o.#", "#........#", "#..@.....#", "#........#", "##########"],
    ["##########", "#........#", "#.#..o...#", "#.#.$....#", "#.#......#", "#.@......#", "##########"],
    ["##########", "#........#", "#..o#....#", "#...#....#", "#...$....#", "#...@....#", "##########"],
]
MAZE = [
    "##################",
    "#@...#.....#....b.#",
    "#.##.#.###.#.###.#",
    "#.#..#...#...#...#",
    "#.#.####.#####.#.#",
    "#...#..b.....#.#.#",
    "###.#.######.#.#.#",
    "#...#......#...#.#",
    "#.####.###.#####.#",
    "#b.....#.........#",
    "#.######.#######.#",
    "#..............E.#",
    "##################",
]


def grid_map(rows, x0, y0, puzzle):
    m=screen(); player=(0,0)
    for y,row in enumerate(rows):
        for x,char in enumerate(row):
            m[(y+y0)*32+x+x0]={"#":1,".":0,"@":0,"$":6,"o":5,"b":7,"E":8}[char]
            if char=="@": player=(x+x0,y+y0)
    if puzzle:
        text(m,0,"GARDEN 00/03",0); text(m,2,"PUSH BOX TO FLOWER")
        text(m,14,"B: RESET THIS BOARD"); text(m,16,"MOVES 00",6)
    else:
        text(m,0,"SIGNAL 00/03",0); text(m,1,"BATTERY 90",0)
        text(m,17,"FIND 3 THEN EXIT")
    return m,player


def grid_game(a,puzzle):
    maps={}
    starts=[]
    for n,rows in enumerate(GARDENS if puzzle else [MAZE]):
        m,p=grid_map(rows,5 if puzzle else 1,5 if puzzle else 3,puzzle)
        maps[f"board_{n}"]=m; starts.append(p)
    a.label("initialize"); a.jp("board_start")
    a.label("board_start")
    if puzzle:
        a.load(SCORE); a.emit(0xB7); a.jp("board_pick_0","z")
        a.emit(0xFE,1); a.jp("board_pick_1","z")
    for n,(x,y) in reversed(list(enumerate(starts))):
        a.label(f"board_pick_{n}")
        a.value(PX,x); a.value(PY,y); a.pair("de",f"board_{n}"); a.jp("board_copy")
    a.label("board_copy")
    a.io(0x40,0); a.pair("hl",BOARD); a.pair("bc",1024); a.call("copy")
    a.pair("de",BOARD); a.call("show_map"); a.value(AUX,0); a.value(STEP_CLOCK,0); a.ret()
    a.label("game_step")
    if puzzle:
        a.test_key(B_BUTTON,"grid_not_reset"); a.call("board_start"); a.ret(); a.label("grid_not_reset")
    else:
        a.inc(SUBSECOND); a.load(SUBSECOND); a.emit(0xFE,60); a.jp("grid_clock_done","c")
        a.value(SUBSECOND,0); a.dec(SECONDS); a.load(SECONDS); a.emit(0xB7); a.jp("lose","z")
        a.label("grid_clock_done")
    # Edges move immediately; holding repeats at a measured eight-frame cadence.
    a.load(EDGE); a.emit(0xE6,15); a.jp("grid_move","nz")
    a.load(STEP_CLOCK); a.emit(0xB7); a.jp("grid_move","z")
    a.dec(STEP_CLOCK); a.ret()
    a.label("grid_move"); a.value(STEP_CLOCK,7)
    a.load(PX); a.emit(0x47); a.load(PY); a.emit(0x4F)
    a.test_key(LEFT,"grid_right",True); a.emit(0x05); a.value(DIRECTION,0); a.jp("grid_destination")
    a.label("grid_right"); a.test_key(RIGHT,"grid_up",True); a.emit(0x04); a.value(DIRECTION,1); a.jp("grid_destination")
    a.label("grid_up"); a.test_key(UP,"grid_down",True); a.emit(0x0D); a.value(DIRECTION,2); a.jp("grid_destination")
    a.label("grid_down"); a.test_key(DOWN,"grid_done",True); a.emit(0x0C); a.value(DIRECTION,3)
    a.label("grid_destination")
    a.emit(0x78); a.store(EX); a.emit(0x79); a.store(EY)
    a.call("grid_address"); a.emit(0x7E,0xFE,1); a.jp("grid_done","z")
    if puzzle:
        a.emit(0xFE,6); a.jp("grid_accept","nz")
        # Move one more cell in the same direction before allowing the push.
        a.load(DIRECTION); a.emit(0xB7); a.jp("push_left","z")
        a.emit(0xFE,1); a.jp("push_right","z")
        a.emit(0xFE,2); a.jp("push_up","z")
        a.emit(0x0C); a.jp("push_address")
        a.label("push_left"); a.emit(0x05); a.jp("push_address")
        a.label("push_right"); a.emit(0x04); a.jp("push_address")
        a.label("push_up"); a.emit(0x0D)
        a.label("push_address"); a.call("grid_address"); a.emit(0x7E,0xFE,1); a.jp("grid_done","z")
        a.emit(0xFE,6); a.jp("grid_done","z")
        a.emit(0xFE,5); a.jp("garden_solved","z")
        a.emit(0x3E,6); a.call("paint_cell")
        a.load(EX); a.emit(0x47); a.load(EY); a.emit(0x4F); a.call("grid_address")
        a.emit(0xAF); a.call("paint_cell"); a.call("good"); a.jp("grid_accept")
        a.label("garden_solved")
        a.inc(SCORE); a.call("good"); a.load(SCORE); a.emit(0xFE,3); a.jp("win","z")
        a.jp("board_start")
    else:
        a.emit(0xFE,7); a.jp("maze_exit","nz")
        a.emit(0xAF); a.call("paint_cell"); a.inc(SCORE); a.call("good"); a.jp("grid_accept")
        a.label("maze_exit"); a.emit(0xFE,8); a.jp("grid_accept","nz")
        a.load(SCORE); a.emit(0xFE,3); a.jp("win","z")
    a.label("grid_accept")
    a.load(EX); a.store(PX); a.load(EY); a.store(PY)
    if puzzle:
        a.load(AUX); a.emit(0xFE,99); a.jp("grid_done","nc"); a.inc(AUX)
    a.label("grid_done"); a.ret()
    a.label("draw")
    # Convert the tile coordinates to real hardware OAM coordinates.
    a.load(PX); a.emit(0x87,0x87,0x87,0xC6,8); a.store(0xFE01)
    a.load(PY); a.emit(0x87,0x87,0x87,0xC6,16); a.store(0xFE00)
    a.value(0xFE02,131 if puzzle else 132); a.value(0xFE03,0)
    a.load(SCORE); a.pair("hl",0x9807); a.call("number")
    a.load(AUX if puzzle else SECONDS); a.pair("hl",0x9800+16*32+12 if puzzle else 0x9828); a.call("number")
    a.ret()
    return maps


def orbit(a):
    m=screen()
    text(m,0,"HITS 00/15   LIVES 3",0)
    for x,y in [(2,3),(7,4),(15,2),(18,5),(4,7),(11,6),(2,10),(15,11),(9,9),(7,13),(17,14)]: m[y*32+x]=9
    for x in range(20): m[16*32+x]=2
    text(m,17,"HOLD A TO FIRE",3)
    a.label("initialize"); load_map(a,"game_map")
    a.value(PX,76); a.value(PY,116); a.value(EX,36); a.value(EY,24); a.value(DIRECTION,1)
    a.ret()
    a.label("game_step"); horizontal(a)
    a.load(BULLET); a.emit(0xB7); a.jp("orbit_bullet","nz")
    a.test_key(A_BUTTON,"orbit_enemy",held=True)
    a.value(BULLET,1); a.load(PX); a.store(BX); a.value(BY,110); a.call("fire")
    a.label("orbit_bullet")
    a.load(BY); a.emit(0xD6,4); a.store(BY); a.emit(0xFE,16); a.jp("orbit_enemy","nc")
    a.value(BULLET,0)
    a.label("orbit_enemy")
    a.load(TICK); a.emit(0xE6,1); a.jp("orbit_collision","nz")
    a.load(DIRECTION); a.emit(0xB7); a.jp("orbit_enemy_left","z")
    a.inc(EX); a.load(EX); a.emit(0xFE,144); a.jp("orbit_collision","c")
    a.value(DIRECTION,0); a.jp("orbit_drop")
    a.label("orbit_enemy_left")
    a.dec(EX); a.load(EX); a.emit(0xFE,8); a.jp("orbit_collision","nz")
    a.value(DIRECTION,1)
    a.label("orbit_drop")
    a.load(EY); a.emit(0xC6,12); a.store(EY); a.emit(0xFE,112); a.jp("orbit_collision","c")
    a.dec(LIVES); a.call("crash"); a.load(LIVES); a.emit(0xB7); a.jp("lose","z")
    a.jp("orbit_respawn")
    a.label("orbit_collision")
    a.load(BULLET); a.emit(0xB7); a.jp("orbit_done","z")
    # abs(delta)<8 using unsigned(delta+7)<15 on the 8-bit coordinate ring.
    a.load(EX); a.emit(0x47); a.load(BX); a.emit(0x90,0xC6,7,0xFE,15); a.jp("orbit_done","nc")
    a.load(EY); a.emit(0x47); a.load(BY); a.emit(0x90,0xC6,7,0xFE,15); a.jp("orbit_done","nc")
    a.inc(SCORE); a.call("good"); a.load(SCORE); a.emit(0xFE,15); a.jp("win","z")
    a.label("orbit_respawn")
    a.value(BULLET,0); a.value(EY,24)
    a.load(SCORE); a.emit(0x87,0x87,0x87,0xE6,0x7F,0xC6,8); a.store(EX)
    a.label("orbit_done"); a.ret()
    a.label("draw")
    sprite(a,0,PX,PY,133); sprite(a,1,EX,EY,134)
    a.load(BULLET); a.emit(0xB7); a.jp("hide_pulse","z")
    sprite(a,2,BX,BY,135); a.jp("orbit_hud")
    a.label("hide_pulse"); a.value(0xFE08,0)
    a.label("orbit_hud"); hud(a,"HITS"); a.ret()
    return {"game_map":m}


def build(game):
    a=Assembler()
    shared(a,game)
    maps = {"pocket-racer":racer,"moon-courier":courier,"matcha-garden":lambda a:grid_game(a,True),
            "signal-lost":lambda a:grid_game(a,False),"orbit-guard":orbit}[game["id"]](a)
    for name,m in {"title_map":title_map(game),"win_map":ending_map(game,True),"lose_map":ending_map(game,False),**maps}.items():
        a.label(name); a.code.extend(m)
    a.label("tiles"); a.code.extend(tiles())
    code=a.finish()
    if len(code)>32768-0x150: raise ValueError("cartridge exceeds ROM-only capacity")
    rom=bytearray(32768)
    rom[0x100:0x104]=bytes([0x00,0xC3,0x50,0x01])
    # Fixed compatibility header required by the Game Boy boot ROM, not in-game art.
    rom[0x104:0x134]=bytes.fromhex("CE ED 66 66 CC 0D 00 0B 03 73 00 83 00 0C 00 0D 00 08 11 1F 88 89 00 0E DC CC 6E E6 DD DD D9 99 BB BB 67 63 6E 0E EC CC DD DC 99 9F BB B9 33 3E")
    title=game["title"].upper().encode("ascii")[:15]
    rom[0x134:0x134+len(title)]=title
    rom[0x14A]=1
    rom[0x150:0x150+len(code)]=code
    checksum=0
    for byte in rom[0x134:0x14D]: checksum=(checksum-byte-1)&255
    rom[0x14D]=checksum
    rom[0x14E:0x150]=(sum(rom)&65535).to_bytes(2,"big")
    entry={key:game[key] for key in ("id","title","controls","goal","learn","inspector_hint")}
    entry.update(system="GB",source="games/gb/build_games.py",rom=f"games/gb/roms/{game['id']}.gb",
                 license="GPL-3.0-only",sha256=hashlib.sha256(rom).hexdigest(),rom_bytes=len(rom),
                 provenance="Original assistant-authored code, lettering, tile art, levels and synthesized sound, produced in Codex under the project owner's direction.",
                 state_addresses={"state":"C000: 0 title, 1 playing, 2 won, 3 lost","score":"C005","lives":"C006","x":"C007","y":"C008"})
    return bytes(rom),entry,a.labels


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check",action="store_true",help="Verify checked-in ROMs and manifest are byte-for-byte reproducible.")
    args=parser.parse_args()
    entries=[]
    (ROOT/"roms").mkdir(exist_ok=True)
    for game in GAMES:
        rom,entry,_=build(game)
        path=ROOT/"roms"/f"{game['id']}.gb"
        if args.check:
            if path.read_bytes()!=rom: raise SystemExit(f"stale cartridge: {path}")
        else: path.write_bytes(rom)
        entries.append(entry)
        print(f"{game['id']}: {len(rom)} bytes, {entry['sha256']}")
    manifest={"schema_version":1,"license":"GPL-3.0-only","games":entries}
    data=json.dumps(manifest,indent=2)+"\n"
    if args.check:
        if (ROOT/"manifest.json").read_text(encoding="utf-8")!=data: raise SystemExit("stale manifest")
    else: (ROOT/"manifest.json").write_text(data,encoding="utf-8")


if __name__=="__main__": main()
