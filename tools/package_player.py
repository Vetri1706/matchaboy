"""Prepare the portable native player, notices and corresponding rebuild source."""
import argparse
from pathlib import Path
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[1]

def add_sources(destination):
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT/'THIRD_PARTY.md', destination/'THIRD_PARTY.md')
    licenses = destination/'licenses'; licenses.mkdir(exist_ok=True)
    for source, name in [('LICENSE','mGBA-MPL-2.0.txt'),
                         ('src/third-party/blip_buf/license.txt','blip_buf-LGPL-2.1.txt'),
                         ('src/third-party/inih/LICENSE.txt','inih-BSD.txt')]:
        shutil.copy2(ROOT/'third_party/mgba'/source, licenses/name)
    with zipfile.ZipFile(destination/'source.zip','w',zipfile.ZIP_DEFLATED) as archive:
        for directory in ['src','include','cmake','tests','tools','third_party/mgba']:
            for path in sorted((ROOT/directory).rglob('*')):
                relative=path.relative_to(ROOT)
                if path.is_file() and not any(part in {'.git','__pycache__','cinema'} for part in relative.parts):
                    archive.write(path, relative.as_posix())
        for name in ['CMakeLists.txt','README.md','DOWNLOADS.md','PLATFORM.md','PLATFORM_VERIFICATION.md',
                     'VERIFICATION.md','MOONEYE.md','MEALYBUG.md','ARTIFACTS.md','THIRD_PARTY.md','matcha_gym.py']:
            archive.write(ROOT/name, name)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=False)
    shutil.copy2(args.binary,args.output/'Matchaboy.exe')
    add_sources(args.output)
    (args.output/'Read me.txt').write_text('''Matchaboy portable for Windows 10/11 x64

Double-click Matchaboy.exe and choose a .gb or .gba file. No installer,
additional emulator, external BIOS or runtime package is needed.

Arrows: move. Z: A. X: B. Enter: Start. Shift: Select.
GBA: Q is L shoulder, W is R shoulder.
Space: pause. C: hide/show controls. M: mute/unmute. Ctrl+O: open another game.

GBA cartridge saves use <game>.matchaboy.sav beside the ROM and are written
when closing normally or switching games. Use a writable game folder.
Game Boy Color-only games and GBA netplay are not supported. Speaker audio
uses Windows built-in playback; no audio package installation is needed. The hardware Inspector is for the original Game Boy.

source.zip and licenses are included for attribution and rebuilding/relinking;
they do not need to be extracted or installed to play. See THIRD_PARTY.md.
To rebuild, extract source.zip and follow DOWNLOADS.md's Windows build steps.
Use CMake Release and MATCHA_BUILD_AUTOPSY=ON; no third-party downloads needed.
''')
    print(args.output.resolve())

if __name__=='__main__': main()
