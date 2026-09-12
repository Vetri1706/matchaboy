# Play with a friend

Use the same Matchaboy build and the exact same ROM revision on both computers.
Each friend controls a separate linked console and keeps their own saved game.
The Mac, Windows and Linux desktop players expose **Netplay > Host Game** and
**Join Game** using the same FriendSession implementation. The original
command-line `netplay` rollback verifier remains a separate tool.

The Windows interactive session and Linux desktop changes in this revision
await native CI evidence. Sharing a protocol is not yet proof of every
mixed-platform ROM/session combination; the retained FIFA gameplay evidence
below was collected on Mac.

## Same Wi-Fi

1. Both friends open their own copy of the ROM with **Open game…** (under
   Game on Mac or File on Windows/Linux).
2. The host chooses **Netplay > Host Game**, normally leaving UDP port **27888**.
   Hosting restarts the game from saved progress. Copy the generated room code.
3. The friend chooses **Netplay > Join Game**, enters the host computer's local IPv4
   address, the same port, and the room code. The host's network settings
   show its local IPv4 address. Guest Wi-Fi/client isolation can block peers.
4. Wait for the player/frame status to show a linked connection. Enter the
   game's own Link Cable or Multiplayer menu on each console.
5. Use **Netplay > Disconnect** when finished. Both apps keep their own local
   console state; only the local save is written. No ROM is sent to the peer.

Allow Matchaboy incoming traffic if the operating-system firewall prompts. The program does
not change firewall or router settings automatically. **Connection Details**
shows the port and status, with separate **Copy room code** and **Copy
diagnostics** buttons.

## Internet

The same Host/Join flow works with a reachable host address. A private VPN that
connects the two computers is the preferred route: enter the host's VPN IPv4
address. Alternatively, forward the chosen **UDP** port on the host's router
to the host computer and join using its public IPv4 address. Carrier-grade NAT or
networks that prohibit inbound traffic need a VPN or a separately provided
reachable network. No public relay, matchmaking service, UPnP, or automatic
NAT traversal is bundled or deployed.

The UDP room token and session payloads are plaintext. A room code selects a
session; it is not encryption or authenticated identity. Saved progress is
exchanged to reproduce both consoles. Use a trusted LAN or an encrypted VPN
when that traffic must remain private. Do not publish a room code.

## FIFA 07

After both apps connect, choose **Multiplayer** on both title menus. When the
game recognizes two consoles, continue, assign one player to Home and the
other to Away, accept the sides and teams, then choose **Start Match**. Each
player has their own keyboard. With the default **Balanced** preset, **WASD**
controls the D-pad, **A** means **L**, **B** means **K**, **Start** means
**Return/Enter**, **Select** means **Space**, and **L/R shoulders** mean **Q/I**.
Space is not fast-forward. Press **Command-Shift-C** on Mac or **Ctrl-Shift-C**
on Windows/Linux for the guide, which shows
the active game button → key mapping and marks locally held keys with `*`.
Consult FIFA's own controls for each console button's action.

Use **Matchaboy > Keyboard Settings…** (**Command-Comma**) on Mac, or
**Tools > Keyboard Settings…** (**Ctrl-Comma**) on Windows/Linux to choose Balanced,
Classic, or your own bindings. Click a binding and press a key, or use **Set all keys…**;
**Apply** saves the mapping across launches and **Cancel** preserves the previous
one. Classic supplies arrows/Z/X/Shift and Q/W shoulders. Keys name physical ANSI
positions. Both friends may use different mappings because the session exchanges
console button inputs, not keyboard keys. The mapping does not change input delay.
**Escape** pauses only local play; disconnect to stop a linked session.

The user-provided FIFA 07 cartridge completed this actual sequence in two
independent emulator processes: 5,149 frames per peer, including 1,000 gameplay
frames after kickoff. All 85 periodic peer-state checks through frame 5,100
matched. Kickoff and gameplay framebuffer captures for both players matched
the corresponding direct-cable run byte for byte. The impaired run used real
UDP with 30–70 ms one-way delay and seeded 5% packet loss. These tests ran on
one Mac with separate processes and real sockets; they do not verify a second
physical Mac or a public internet route.

Separately, on 2026-09-12 the user reported successful FIFA play between two
physical Macs, followed by intermittent mid-session connection losses. This is
a user report, not a second-Mac test independently observed by the automated
harness. The exact disconnect reason has not been captured. The user clarified
that the controls felt unfamiliar because of the mapping, rather than latency.

No commercial ROM or save is included in the app or repository. The optional
`tools/verify_fifa_netplay.py` harness takes the user's ROM path, makes isolated
temporary copies, and verifies the originals are unchanged.

## Timing, saves, and compatibility

Both computers run the same pair of real linked consoles. They exchange inputs
six frames ahead (about 100 ms), then advance only when both inputs are known.
Local cable events retain emulated hardware timing; network delays never move
an individual cable edge. This friend player uses input delay and confirmed
frames, not speculative rollback. Audio/video are produced only for completed
frames. A late network packet can pause playback; it is never replaced with a
guessed committed input. State mismatch or connection failure stops the session.

Ten seconds without valid peer traffic closes an established connection. There
is no automatic reconnect; copy diagnostics before using **Disconnect**, then
start a new Host/Join session. Real-socket tests recover accepted messages after
a three-second bidirectional packet blackout and a three-second peer polling
pause, while preserving the ten-second terminal timeout and 100 ms retry timer.

While a friend session is unfinished, the Mac app uses
`NSActivityUserInitiatedAllowingIdleSystemSleep` to suppress App Nap. Its nominal
240 Hz service timer also runs in `NSModalPanelRunLoopMode`, independently of
whether a video frame is due. This permits normal system sleep and does not
change system power settings. Windows settings/connection dialogs keep
dispatching the main player timer, and Linux keeps its network service in the
main event loop while dialogs are open. These are platform-specific liveness
mechanisms, not a confirmed fix for the user's still-undiagnosed disconnect.

For a failure report, open **Netplay > Connection Details > Copy diagnostics**
before disconnecting. The text includes status, current and last verified
frames, queued inputs, sent/accepted-received packet counts, retransmissions,
time without peer traffic, and on Mac whether App Nap prevention is active. It omits
the room code, address, ROM path, and save contents. Packet counters and silence
at transport closure remain available until the session is discarded.

Single-player pause, instruction/dot stepping, and changing games are disabled
while linked. Losing focus releases buttons but keeps the connection running.
Muting, screenshots, and read-only Inspector panels remain available. Canceling
connection preparation retains the old game. Disconnect before switching ROMs.

GBA normal 8/32-bit and multiplayer cable modes execute through the bundled
core's real serial driver. The linked GBA RTC advances from a shared UTC epoch
of 2000-01-01; host-local saved RTC offsets are intentionally not used while
linked. Ordinary offline RTC behavior is restored on disconnect.

Game Boy master/external-slave transfers have passed 64 clock-phase alignments
and a 1,000-frame network run. Some competing internal-clock schedules are not
supported and fail explicitly. STOP during a linked Game Boy session is also
unsupported. Color-only Game Boy cartridges and GBA wireless-adapter emulation
are outside this build. Pokémon trading/battling has **not** been verified with
a Pokémon ROM; do not assume every version or cross-version pairing works.
Cross-version ROM pairs are currently rejected by the exact-ROM handshake.

Battery-backed GB RAM and GBA progress use `<game>.matchaboy.sav` beside the ROM.
Make an ordinary in-game save before starting a connection. Game Boy `.sav`
imports must be raw cartridge RAM of the expected size and use the Matchaboy
filename; this does not import another emulator's save states or RTC sidecars.
Peer save data stays in memory and is never written over the local player's save.
