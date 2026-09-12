# Games and verification fixtures

The current player library contains the independently authored games documented
in [HOMEBREW.md](../HOMEBREW.md). Their unchanged official ROMs, hashes and license
notices are under [homebrew](homebrew). The native catalog and resource lists
are generated from `homebrew/manifest.json`:

```sh
python tools/update_arcade_catalog.py
python tools/update_arcade_catalog.py --check
```

The five original Game Boy and five original Game Boy Advance mini-games under
[gb](gb) and [gba](gba) are retained as controlled regression fixtures. They are
no longer embedded or installed in the player library. Open their `.gb` / `.gba`
files explicitly to play them, or follow each directory's build instructions to
modify them. Source and recorded [provenance](PROVENANCE.md) remain available;
these authored fixtures are GPL-3.0-only.

The [previous Windows verification](verification/) records the original ten-game
library milestone. It is historical evidence, not the current bundled catalog.
No existing user's cartridge files or saves are deleted by the replacement.
