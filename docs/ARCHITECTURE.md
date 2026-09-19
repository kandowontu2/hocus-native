# Hocus Native architecture

This document separates confirmed evidence from implementation decisions. A
name should only be promoted from a hypothesis when the binary, data, or runtime
behaviour supports it.

## Confirmed source material

The local game is registered version 1.1:

- `hocus.exe`: 182,656 bytes, 16-bit real-mode MZ, Borland C++ 1991 runtime.
- `__hpgrvs.exe`: 156,832 bytes, sibling Gravis UltraSound build.
- `hocus.dat`: 6,101,525 bytes.
- The archive FAT begins at file offset `0x1F1A4` in `hocus.exe` and contains
  652 little-endian `(uint32 offset, uint32 size)` pairs.
- All 652 entries are in bounds and contiguous. The last entry ends exactly at
  the end of `hocus.dat`; there are no gaps or trailing bytes.
- The game renders a 320x200 VGA-style display. Level viewports are 320x160;
  the remaining 40 rows are the HUD.
- Each of the 36 registered levels consists of 13 archive entries (`.000` to
  `.012`). Tile layers are 240x60 cells; the event layer is 16-bit.

The source installation hashes and a per-entry asset hash inventory are emitted
by `tools/extract_assets.py` into the local extraction manifest.

## Port boundary

The reconstructed game is split into deterministic game code and native Windows
services:

1. `hocus_core` owns archive access, format decoding, fixed-step game state,
   collision, entities, scripts, and the byte-compatible save representation.
2. `hocus_native` owns the Windows window, event pump, presentation, input, audio,
   timing, filesystem paths, and packaging. Its controller boundary dynamically
   loads the newest available XInput API, checks all four user slots, and falls
   back to the recovered WinMM joystick path without adding a runtime-library
   installation requirement.
3. The registered path retains an indexed 320x200 logical framebuffer so
   palette effects, pixel placement, and original timing can be reproduced.
   Optional native widescreen gameplay expands the world/HUD framebuffer to
   356x200 (16:9), 467x200 (21:9), or 711x200 (32:9), while menus and fixed
   artwork remain 320x200. Win32 presentation converts/scales the logical
   image rather than emulating VGA hardware.
4. The audited archive index is a native compile-time table and the user's
   registered-v1.1 `HOCUS.DAT` is embedded as a Windows resource. The native
   runtime does not execute DOS code or read assets from the DOS files, but it
   requires the exact registered-v1.1 `HOCUS.EXE` beside `hocus_native.exe` and
   verifies its 182,656-byte SHA-256 identity before initialization.

This is a native replacement boundary, not a claim that every DOS routine has
an instruction-identical translation. The current discovered-function ledger,
behavioral corrections, and native-boundary qualifications are maintained in
[`PARITY_AUDIT.md`](PARITY_AUDIT.md).

## Reconstruction sequence

The first vertical slice has expanded into a data-driven registered-level
runner. All 36 levels select their original records, visual sets, starts, maps,
events, and entity definitions in a pannable native viewport. Recovered integer
movement, jumping, event sampling, projectiles, breakable walls, switches,
keyholes, one-way teleporter sequences, ordinary enemy selectors 0 through 6,
and the selector-8 Episode Three boss execute in `hocus_core`; E3/E4 elevator
pairs also mutate the original collision layer and carry the player. Enemy
movement/attack frame ranges and embedded projectile ranges, offsets, collision
sizes, and selector-5 launch delay come from the recovered sprite headers.
Native VOC effects and MIDI music cross deterministic core/frontend boundaries,
and the original menu, episode, skill, story, help, credits, endings, results,
high scores, pause, save, options, and five-recording attract-mode flows drive
the front end.

## Save boundary

`SaveFile` retains the full registered-v1.1 990-byte `HOCUS.SAV` block. The
recovered slot and high-score fields are interpreted and changed; all unrelated
bytes survive a native round trip byte-for-byte. The save service performs
typed validation and writes a completed
temporary image, makes `HOCUS.SAV.bak` once, and replaces the live file only
after the player confirms a save in the native menu.

Volume, game speed, joystick preference/calibration, and all eight key bindings
use the recovered registered-v1.1 fields in `HOCUS.SAV`. Native virtual-key
codes are derived from the DOS binding indices when the save is loaded. The
same joystick-enabled word selects native XInput when an Xbox-compatible pad is
connected; XInput uses fixed dead zones and mappings rather than overwriting the
legacy calibration fields.

## Known executable data (registered v1.1)

These file offsets are evidence-backed starting points for typed table recovery:

| File offset | Data |
|---:|---|
| `0x1F1A4` | archive FAT |
| `0x20680` | 16 sound mappings |
| `0x20A22` | story picture table |
| `0x20A9A` | per-level time limits |
| `0x21714` | item definitions |
| `0x21ADA` | per-level tileset indices |
| `0x21B2A` | level-number table |
| `0x21B7A` | per-level backdrop indices |
| `0x21BDE` | per-level music indices |
| `0x21C26` | elevator tile pairs |
| `0x21CC6` | episode-four boss tables |
| `0x21CEE` | super-jump vertical increments |
| `0x21D20` | normal-jump vertical increments |
| `0x21D46` | damage increments |
