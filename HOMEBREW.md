# Included homebrew

The player library now contains **Tobu Tobu Girl** and **Tobu Tobu Girl Deluxe**,
by **Tangram Games**, with original music by **potato-tan**. Both are single-player
arcade platformers. Deluxe uses its original Game Boy compatibility mode in
Matchaboy; this is not Game Boy Color emulation.

| Game | Official project | Code / assets |
| --- | --- | --- |
| Tobu Tobu Girl | https://tangramgames.itch.io/tobutobugirl | MIT / CC BY 4.0 |
| Tobu Tobu Girl Deluxe | https://tangramgames.itch.io/tobu-tobu-girl-deluxe | MIT / CC BY 4.0 |

Choose a title in Library, then **Play game**. Enter skips the opening story and
opens the game menu; use the on-screen game instructions to select a stage.
With the default Balanced mapping, WASD steers, L is the game's A button (dash),
and K is B (boost). Game button names in the ROM are translated by the app's
controls guide; Keyboard Settings changes those bindings.

The complete credits and license terms are available through **Game > Game
Credits and Licenses** on Mac/Linux or **File > Game credits and licenses** on
Windows, and in `Extras/licenses/homebrew/CREDITS.txt` in downloads. The Mac app
and Windows executable also carry the full notices internally. Linux keeps them
in `assets/licenses/CREDITS.txt`; keep the assets directory beside the player.

`games/homebrew/manifest.json` records the exact official ROM hashes, filenames,
source/asset license declarations pinned to upstream commits, and bounded boot
and input verification plans. ROM bytes have not been patched. The upstream
filenames `tobu.gb` and `tobudx.gb` are renamed only to match stable library IDs.
`tools/update_arcade_catalog.py --check` validates hashes, notices and generated
native resource lists. Normal builds and gameplay do not download games.

The previous ten originals are no longer in the player library. Their source
and cartridges remain under `games/gb` and `games/gba` as controlled hardware,
input and networking test fixtures. Existing user saves are never removed.
The optional source archive contains those fixtures for rebuilding the tests.

## Other suggested games

These entries are not included in the download. Public source or a free download
alone does not establish permission to redistribute every asset inside a ROM.

- **BlindJump:** [official project](https://github.com/evanbowman/blind-jump-portable).
  Its README distinguishes MIT code, a GPL GBA build, noncommercial artwork and
  separately licensed music. Complete ROM redistribution conditions remain
  unresolved. The author describes link co-op as incomplete; Matchaboy gameplay
  compatibility has not been verified.
- **Anguna:** [official download](https://www.bitethechili.com/anguna/).
  The current GBA ZIP lacks a redistribution notice. The author's separate DS
  source includes permissions and asset exceptions, which we have not assumed
  apply to the GBA release.
- **OpenLara:** [official source](https://github.com/XProger/OpenLara).
  Its BSD engine license does not grant rights to the Tomb Raider level assets
  inside the GBA build.
- **GBADoom / Freedoom:** [GBADoom](https://github.com/doomhack/GBADoom) explicitly
  has no multiplayer. [Freedoom](https://github.com/freedoom/freedoom) provides
  separately licensed WAD assets, not a ready verified GBA ROM. A suitable build
  and its corresponding source/notices would need separate integration. The
  ready [GBAFreeDoom 0.7 fork](https://github.com/RetroGamer02/GBAFreeDoom/releases/tag/0.7)
  retains a baked [Doom II HUD](https://github.com/RetroGamer02/GBAFreeDoom/blob/0.7/source/gfx/stbar.h)
  outside its replaced WAD; it is not included either.
- **Pokémon decompositions:** [pokeruby](https://github.com/pret/pokeruby) and
  [pokefirered](https://github.com/pret/pokefirered) reconstruct retail games;
  their availability does not establish a game-content redistribution license.
- **Morphcat:** the likely reference, [Micro Mages](https://morphcat.de/micromages/),
  targets NES/PC, which Matchaboy does not emulate.
- **Link demos:** the suggested `sys/link_multi` and `sys/link_normal` paths were
  not present in the canonical examples repositories. Actual MIT-licensed
  [gba-link-connection demos](https://github.com/afska/gba-link-connection) are
  useful diagnostics, but are not full games in this library.

To play with a friend, open your own compatible link-enabled game on both
machines and follow [NETPLAY.md](NETPLAY.md). The two included Tobu titles do not
provide multiplayer, so starting an emulator connection cannot add it to them.

## Verified build

[Native CI run 34683563203](https://github.com/Vetri1706/matchaboy/actions/runs/34683563203)
passed on all three platforms for source commit `370bfee` on 2026-09-12.
The player tests run the exact licensed ROMs, compare controller input against
fresh equally long idle runs, check mapped controls and readable creator metadata,
and retain separate GB/GBA hardware, save and real-UDP regression fixtures.
Each downloadable ZIP is extracted and exercised outside the source checkout;
its build manifest records the exact binary/file hashes. Mac packages also
verify the completed app's resource signature, including the license notices.
They use ad-hoc signing and are not Apple-notarized.

These checks establish boot, controller response and integration, not completion
of every level or multiplayer support for these two single-player games.
