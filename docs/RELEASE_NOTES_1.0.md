# Hocus Native 1.0.0

The first public source release of the from-scratch native Windows port of the
registered-v1.1 MS-DOS game *Hocus Pocus*.

## Highlights

- Native Win32 presentation with no DOS emulation
- All 36 registered levels and the complete front end
- Recovered movement, collision, events, pickups, switches, gates,
  teleporters, elevators, enemies, bosses, projectiles, effects, and HUD
- Original startup, menus, help/story/credits/endings, saves, high scores,
  attract demos, VOC effects, MIDI music, and joystick support
- Optional 125 Hz interpolated High FPS mode
- Borderless fullscreen with Alt+Enter
- Ctrl+Alt+F1 persistent cheat menu, including Jump in Mid-Air and a selector
  for all 36 chapter/stage combinations
- Static C/C++ runtime linkage; locally built executables require no runtime
  library installation
- Exhaustive registered-v1.1 disassembly and behavioral parity ledger

## Getting the game

This source release contains no commercial game data and no prebuilt binary.
Purchase *Hocus Pocus* from [GOG](https://www.gog.com/en/game/hocus_pocus) or
[Steam](https://store.steampowered.com/app/358290/Hocus_Pocus/), then place the
registered-v1.1 `HOCUS.EXE` and `HOCUS.DAT` in the source directory before
building. The build embeds your own `HOCUS.DAT`; the finished program validates
the adjacent registered-v1.1 `HOCUS.EXE` at runtime.

See [README.md](../README.md) for build and usage instructions and
[CREDITS.md](../CREDITS.md) for the full original-game and port credits.

## Validation

The release passes all eight CTest targets, including asset/gameplay smoke,
standalone runtime, registered executable validation, static-runtime import
inspection, disassembly inventory, extraction pipeline, executable tables, and
parity ledger tests.
