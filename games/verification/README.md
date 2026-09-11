# Integrated Windows arcade verification

`windows-library.json` records the exact final executable and ten embedded ROM
hashes tested on Windows on 12 September 2026. The executable was copied alone
into a new Unicode-path directory, with only Windows System32 on its PATH.
No separately installed game assets or emulator were used.

The test opened the library, selected and launched all ten games, sent real
Start and direction input, captured changed gameplay screens, opened Inspector,
and checked that returning to the library releases keys and suspends emulation.
It also checked that returning to play restores the previous pause state and
that the extracted games exactly match the embedded source artifacts.

Reproduce with `python tools/test_arcade_windows.py --binary <Matchaboy.exe>
--output <new-directory>` from the repository. The PNGs here are actual native
OpenGL captures from that integrated run, not mockups.

Full-objective game replays and speaker checks are recorded separately in
`../gb/verification/` and `../gba/verification/`. Their reports identify the ROM
hashes and test scope; some platform-specific checks use standalone ROM loading
and earlier player builds with the same emulation core. They do not establish
human playtesting, sound quality, physical-console compatibility or a published
release. All eight existing native CTest suites passed for the integrated build.
