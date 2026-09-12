"""Prepare the portable native player, notices and corresponding rebuild source."""
import argparse
from pathlib import Path
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[1]

def add_sources(destination):
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT/'LICENSE', destination/'LICENSE')
    shutil.copytree(ROOT/'assets', destination/'assets', dirs_exist_ok=True)
    shutil.copy2(ROOT/'THIRD_PARTY.md', destination/'THIRD_PARTY.md')
    licenses = destination/'licenses'; licenses.mkdir(exist_ok=True)
    for source, name in [('LICENSE','mGBA-MPL-2.0.txt'),
                         ('src/third-party/blip_buf/license.txt','blip_buf-LGPL-2.1.txt'),
                         ('src/third-party/inih/LICENSE.txt','inih-BSD.txt')]:
        shutil.copy2(ROOT/'third_party/mgba'/source, licenses/name)
    with zipfile.ZipFile(destination/'source.zip','w',zipfile.ZIP_DEFLATED) as archive:
        for directory in ['assets','games','src','include','cmake','tests','tools','third_party/mgba']:
            for path in sorted((ROOT/directory).rglob('*')):
                relative=path.relative_to(ROOT)
                if path.is_file() and not any(part in {'.git','__pycache__','cinema','build'} for part in relative.parts):
                    archive.write(path, relative.as_posix())
        for name in ['LICENSE','CMakeLists.txt','Makefile','DESIGN.md','MACOS.md','WINDOWS.md','LINUX.md','NETPLAY.md','README.md','DOWNLOADS.md','PLATFORM.md','PLATFORM_VERIFICATION.md',
                     'VERIFICATION.md','MOONEYE.md','MEALYBUG.md','ARTIFACTS.md','THIRD_PARTY.md','matcha_gym.py']:
            archive.write(ROOT/name, name)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=False)
    shutil.copy2(args.binary,args.output/'Matchaboy.exe')
    extras = args.output/'Extras'
    add_sources(extras)
    for name in ('WINDOWS.md','LINUX.md','MACOS.md','NETPLAY.md','DOWNLOADS.md','README.md'):
        shutil.copy2(ROOT/name,extras/name)
    shutil.copy2(ROOT/'tools/windows_start_here.txt',args.output/'Start Here.txt')
    print(args.output.resolve())

if __name__=='__main__': main()
