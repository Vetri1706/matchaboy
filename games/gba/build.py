"""Build the original GBA arcade with LLVM only; no GBA SDK or downloaded assets.
SPDX-License-Identifier: GPL-3.0-only
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
NAMES = ['drift-circuit', 'cloud-pilot', 'prism-break', 'tiny-tactics', 'parcel-dash']
TITLES = ['DRIFT CIRCUIT', 'CLOUD PILOT', 'PRISM BREAK', 'TINY TACTICS', 'PARCEL DASH']

def find(name, tool_dir):
    candidate = tool_dir/(name+'.exe' if os.name == 'nt' else name)
    if candidate.exists(): return str(candidate)
    found = shutil.which(name)
    if found: return found
    raise SystemExit(f'Install LLVM with {name}, or pass --llvm-dir.')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--llvm-dir',type=Path,default=Path(r'C:\Program Files\LLVM\bin'))
    parser.add_argument('--test',action='store_true')
    args=parser.parse_args()
    build=ROOT/'build';build.mkdir(exist_ok=True)
    roms=ROOT/'roms';roms.mkdir(exist_ok=True)
    clang=find('clang',args.llvm_dir);lld=find('ld.lld',args.llvm_dir);objcopy=find('llvm-objcopy',args.llvm_dir)
    flags=['--target=arm-none-eabi','-mcpu=arm7tdmi','-marm','-Os','-ffreestanding','-fno-builtin','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-fno-stack-protector','-Wall','-Wextra','-Werror','-I',str(ROOT/'src')]
    common=[]
    for source in ['start.s','game.c','draw.c']:
        obj=build/(source+'.o');subprocess.run([clang,*flags,'-Wno-unused-command-line-argument','-c',str(ROOT/'src'/source),'-o',str(obj)],check=True);common.append(str(obj))
    hashes={}
    for kind,name in enumerate(NAMES):
        mainobj=build/(name+'.o')
        subprocess.run([clang,*flags,f'-DGAME_KIND={kind}','-c',str(ROOT/'src/main.c'),'-o',str(mainobj)],check=True)
        elf=build/(name+'.elf');rom=roms/(name+'.gba')
        subprocess.run([lld,'-T',str(ROOT/'src/rom.ld'),'--build-id=none','-Map='+str(build/(name+'.map')),*common,str(mainobj),'-o',str(elf)],check=True)
        subprocess.run([objcopy,'-O','binary',str(elf),str(rom)],check=True)
        data=bytearray(rom.read_bytes())
        # Skip-BIOS compatible cartridge header. Manufacturer logo is deliberately
        # blank: no third-party logo/artwork is bundled or claimed as original.
        data[0xA0:0xAC]=TITLES[kind].encode('ascii')[:12].ljust(12,b' ')
        data[0xAC:0xB0]=('MXA'+str(kind)).encode('ascii')
        data[0xB0:0xB2]=b'00';data[0xB2]=0x96;data[0xBC]=1
        data[0xBD]=(-sum(data[0xA0:0xBD])-0x19)&255
        size=max(32768,1<<(len(data)-1).bit_length());data.extend(b'\xff'*(size-len(data)))
        rom.write_bytes(data);hashes[name]=dict(bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
    (ROOT/'roms/sha256.json').write_text(json.dumps(hashes,indent=2)+'\n',encoding='utf-8')
    manifest_path=ROOT/'manifest.json'
    if manifest_path.exists():
        manifest=json.loads(manifest_path.read_text(encoding='utf-8'))
        for game in manifest['games']:game['sha256']=hashes[game['id']]['sha256']
        manifest_path.write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(hashes,indent=2))
    if args.test:
        subprocess.run([sys.executable,str(ROOT/'tests/test_logic.py'),'--llvm-dir',str(args.llvm_dir)],check=True)

if __name__=='__main__':main()
