# Matchaboy original arcade provenance

This collection is being developed in Codex under the Matchaboy maintainer's
direction. On 12 September 2026 the maintainer selected these ten concepts:

| Game Boy | Game Boy Advance |
|---|---|
| Pocket Racer — overhead racing | Drift Circuit — time trials |
| Moon Courier — delivery platformer | Cloud Pilot — aerial obstacle course |
| Matcha Garden — grid puzzle | Prism Break — brick breaker |
| Signal Lost — maze exploration | Tiny Tactics — small tactical battles |
| Orbit Guard — arcade defence | Parcel Dash — delivery score attack |

## What the assistant produced

The assistant authored the game logic, original pixel shapes/font data, levels,
sound patterns, ROM build sources, catalog integration and automated checks.
GB and GBA implementation work was delegated to separate Codex agents while the
main task integrated the portable library. The game manifests identify the
source, game-specific controls, learning notes and ROM hashes.

The code is inspectable evidence of the work. This document is a development
record, not independent proof of the model that generated it. The user calls
the assistant Astra; the Git author field alone cannot establish model identity.
No human playtest or user enjoyment is claimed by an automated test result.

Suggested attribution after the recorded tests pass:

> Ten original handheld mini-games built with Astra in Codex, directed by the
> Matchaboy maintainer. Original game logic, pixel art, levels and sound, with
> source and verification records included.

Do not say the entire emulator and its dependencies were generated as part of
this collection. Matchaboy's original DMG engine predates these games; GBA
emulation uses the separately credited mGBA core. The ten ROMs are independent
homebrew software, not technically exclusive to Matchaboy.

## External tools and content

- No downloaded commercial-game code, sprites, maps, samples or music were
  supplied for this collection. Simple fonts, geometric graphics and short
  hardware sound effects are represented directly in the source.
- Python is a development/build tool for generating data and the GB ROMs.
- LLVM Clang/LLD are development tools for the freestanding GBA program. They
  are not installed or invoked by the shipped player.
- ROM header compatibility bytes are hardware-format data. They are not an
  original Matchaboy logo or game asset; no Nintendo affiliation is asserted.
- The Windows application, mGBA and bundled third-party libraries remain under
  the notices in the repository's `THIRD_PARTY.md` and `LICENSE` files.

## License and redistribution

The original game sources, generated ROM programs, pixel artwork, level data
and sound patterns in this directory are released under **GPL-3.0-only**.
See the repository root `LICENSE` for the complete GNU GPL version 3 text.
Redistribution of these games is permitted under that license, including its
corresponding-source and notice requirements. The portable distribution includes
their sources in `source.zip`. This grant does not cover unrelated games opened
by a user or relicense any third-party component.

## Evidence and limits

Each platform's manifest and README describe the game builds and tests.
`tools/update_arcade_catalog.py --check` checks the catalog and ROM hashes.
Windows UI checks exercise the actual embedded library, normal ROM import,
keyboard input, pause/return behaviour and native GPU captures. Separate game
checks exercise gameplay state transitions. Read their reports before making
claims about tested wins, losses or restarts.

The collection should be presented as small original arcade games. It has not
undergone a commercial-length QA campaign or broad physical-hardware testing.
Human playtesting remains the deciding evidence for difficulty, clarity and fun.
