# Original arcade

Five Game Boy and five Game Boy Advance mini-games are embedded in the Windows
player. Launch `Matchaboy.exe`, select a game in the library and choose Play.
No game download or extra runtime installation is required. **Enter** maps to
Start; each game's title screen and library entry explain its controls and goal.

Use **Ctrl+L** or **File > Game library** to return to the catalog. A running
game pauses while the library is open. **Ctrl+O** still opens your own ROM.
**Tab** opens the existing Inspector; each library entry explains a relevant
hardware detail to explore. These explanations are reading guides, not an
automated debugger tutorial or a claim of exclusive emulator technology.

The program prepares its embedded ROMs in `Matchaboy Data/Library` beside the
executable when that folder is writable, otherwise in the current user's
`LocalAppData/Matchaboy/Library`. No network operation is involved. Keep the
portable application's data folder if preserving cartridge save files matters.

The checked-in ROMs mean an ordinary application build does not require the
game-development toolchains. To modify the games, use the build instructions
under [gb](gb) and [gba](gba), then run:

```text
python tools/update_arcade_catalog.py
python tools/update_arcade_catalog.py --check
```

Rebuild the Windows player to embed the modified ROMs. Its source package
includes the game sources, ROMs, manifests and [provenance/license](PROVENANCE.md).
If an extracted library ROM has changed, reopening it through the built-in
library restores that game's embedded bytes; use Open game for your own edits.

The collection is GPL-3.0-only. It does not change the licenses or authorship of
the emulator's original DMG core, mGBA or other third-party components.

[Integrated Windows verification and screenshots](verification/) record all ten
games launching from the executable alone. Platform-specific reports document
full game objectives and speaker checks. Human playtesting remains pending.
