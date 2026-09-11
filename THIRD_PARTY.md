# Bundled GBA core

The Windows player statically links mGBA 0.10.5, commit
`26b7884bc25a5933960f3cdcd98bac1ae14d42e2`, from
https://github.com/mgba-emu/mgba. The original Matchaboy DMG engine remains
independent. No Qt, SDL, external BIOS, FFmpeg, compression library, or mGBA DLL
is required to run the player. Windows system libraries are still required.

mGBA is copyright its contributors under MPL 2.0. Its compiled third-party
components include blip_buf (Copyright 2003-2009 Shay Green, LGPL 2.1 or later)
and inih (Copyright 2009 Ben Hoyt, BSD 3-clause). Their complete license texts
are in `third_party/mgba/LICENSE`, `src/third-party/blip_buf/license.txt`, and
`src/third-party/inih/LICENSE.txt` beneath the mGBA source directory.

Local mGBA changes:

- `include/mgba-util/common.h`: use Clang's size-correct atomic builtins when
  Clang targets the Windows/MSVC ABI, including 16-bit lockstep fields.
- `version.cmake`: pin version information instead of reading the parent repo.

`cmake/GbaCore.cmake` disables optional libraries/frontends, builds only the
GBA core, and removes Unix libm linkage when using the Windows CRT.

Portable distributions include `source.zip` with the corresponding application
and library source, license notices, and build instructions. The source is not
needed to run the executable; it allows rebuilding/relinking with modified
library code. Do not omit it or the license notices from redistributed packages.

Matchaboy-owned code and original artwork are under GNU GPL version 3 (root `LICENSE`). The notices above remain applicable to their respective library source files. Gearboy was consulted as a debugger organization reference; no Gearboy code or artwork is included.
