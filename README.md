# Hocus Native

**A from-scratch native Windows port of the registered v1.1 MS-DOS release of
*Hocus Pocus*. No DOS emulator required.**

[![Release](https://img.shields.io/github/v/release/kandowontu/hocus-native?label=release)](https://github.com/kandowontu/hocus-native/releases/latest)
[![License](https://img.shields.io/badge/source-MIT-blue.svg)](LICENSE.md)

Hocus Native recreates the original game as a Win32 application while retaining
its levels, movement, menus, saves, sound effects, music, demos, and gameplay
rules. The project covers all 36 levels in the registered game, from E1L1 to
E4L9.

> [!IMPORTANT]
> This repository and its releases contain **no original commercial game
> data**. You must own the registered game and provide your own registered-v1.1
> `HOCUS.EXE` and `HOCUS.DAT`.
>
> [Buy Hocus Pocus on GOG](https://www.gog.com/en/game/hocus_pocus) ·
> [Buy Hocus Pocus on Steam](https://store.steampowered.com/app/358290/Hocus_Pocus/)

## Contents

- [What works](#what-works)
- [Requirements](#requirements)
- [Build and run](#build-and-run)
- [Controls](#controls)
- [Cheat menu](#cheat-menu)
- [Widescreen, High FPS, and fullscreen](#widescreen-high-fps-and-fullscreen)
- [Asset extraction](#asset-extraction)
- [Verification and documentation](#verification-and-documentation)
- [Credits and license](#credits-and-license)

## What works

### Complete game

- All four episodes and all 36 registered levels
- Original piracy, Apogee, and registered-title startup sequence
- Story, help, credits, endings, results, and high-score entry
- Original title/credits/demo attract cycle with all five recorded demos
- All nine DOS-compatible save slots

### Gameplay

- Walking, jumping, collision, elevators, teleporters, switches, and keyed gates
- Crystals, treasures, health, firepower, keys, super shots, and laser shots
- Destructible walls, projectiles, spark trails, enemies, and bosses
- Level messages, crystal tips, death, restart, and completion sequences
- Registered-v1.1 event, enemy, timing, random-number, and camera rules

### Native Windows features

- Native Win32 rendering and input; DOSBox is not used
- Original VOC sound effects and MIDI music through Windows services
- Native XInput support for Xbox-compatible controllers, with legacy WinMM
  joystick fallback
- Borderless fullscreen with `Alt+Enter`
- Selectable 16:9, 21:9, and 32:9 gameplay with additional world visibility
- Optional interpolated High FPS mode
- Static C/C++ runtime linkage—users do not need to install runtime libraries
- `Ctrl+Alt+F1` cheat menu with persistent toggles and level select

For exact parity qualifications and native additions, see the
[parity audit](docs/PARITY_AUDIT.md).

## Requirements

You need:

1. Windows and either MSVC or MinGW-w64
2. CMake 3.20 or newer
3. Ninja
4. Your own registered-v1.1 `HOCUS.EXE` and `HOCUS.DAT`

Place both original files in the repository root before configuring the build:

```text
hocus-native/
├── HOCUS.EXE
├── HOCUS.DAT
├── CMakeLists.txt
└── src/
```

The filenames are case-insensitive on Windows. Shareware, altered, or missing
executables are rejected.

## Build and run

Open PowerShell in the repository directory:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/hocus_native.exe
```

The build embeds your `HOCUS.DAT` in `hocus_native.exe` and copies the required
`HOCUS.EXE` beside it. The finished game therefore needs these two files in the
same directory:

```text
hocus_native.exe
HOCUS.EXE
```

`HOCUS.DAT` is not required beside the finished executable because your copy is
embedded during the build. Do not redistribute the resulting executable: it
contains commercial game data from your copy.

To create a local ZIP package:

```powershell
cmake --build build --target package
```

An existing `HOCUS.SAV` may be placed beside the executable. Otherwise, the
game creates a compatible blank save when you first save. Before modifying an
existing DOS save, Hocus Native creates a one-time backup.

## Controls

| Action | Default control |
|---|---|
| Move | Left / Right Arrow |
| Jump | `Space` |
| Fire | `Ctrl` |
| Interact, read messages, or aim upward | Up Arrow |
| Lower a movable elevator | Down Arrow |
| Scroll the viewport | `Page Up` / `Page Down` |
| Pause menu | `Esc` |
| Toggle fullscreen | `Alt+Enter` |
| Open cheat menu during play | `Ctrl+Alt+F1` |

Use the Arrow keys and `Enter` to navigate menus. Any key advances startup
screens while preserving the Apogee and registered-title sequence.

Enable `Joystick` under Game Options to use a controller. XInput checks all
four Windows controller slots and needs no calibration. Legacy WinMM joysticks
retain the original calibration screen.

| XInput action | Controller input |
|---|---|
| Move / menu navigation | D-pad or left stick |
| Jump / confirm | `A` |
| Fire | `X`, `B`, or right trigger |
| Pause | `Start` |
| Cancel / back | `B` or `Back` |
| Scroll viewport down / up | Left / right shoulder |

## Cheat menu

Press `Ctrl+Alt+F1` during a level. Use Up/Down or keys `1`–`6` to select an
entry, then press `Enter` or `Space`. Press `Esc` or `Ctrl+Alt+F1` to close the
menu.

| Entry | Effect |
|---|---|
| `FEELGOOD` | Full health and invincibility |
| `BLAKE` | Infinite silver and gold keys |
| `QUARK` | Permanent rapid fire |
| `BANANA` | Infinite laser shots |
| `JUMP IN MID-AIR` | Jump again while airborne; the camera follows vertically and ejects Hocus from embedded floors |
| `CHAPTER/STAGE SELECT` | Warp to any level from E1L1 through E4L9 |

The four original typed cheat codes remain available as one-shot effects.

## Widescreen, High FPS, and fullscreen

`WIDESCREEN MODE` appears on the main menu with shortcut `W`. Activating it
cycles through `OFF`, `16:9`, `21:9`, and `32:9`. These modes use 320×200,
356×200, 467×200, and 711×200 logical framebuffers respectively, drawing
progressively more map, entity, and projectile space on both sides rather than
stretching the image. The original status bar remains centred exactly once,
with solid-black side panels. Enemy encounters retain their original
player-contact trigger timing in every aspect ratio. Menus, startup screens,
story pages, and other original artwork remain at their unmodified 320×200
dimensions. The setting persists in `HOCUS_NATIVE.CFG`.

`HIGH FPS MODE` appears on the main menu with shortcut `F`. When enabled, it
presents up to 125 interpolated frames per second while leaving the original
fixed-step physics, enemy logic, timers, random stream, and demos unchanged.
The setting persists in `HOCUS_NATIVE.CFG`.

Press `Alt+Enter` at any time to toggle borderless fullscreen. Alt does not
activate the Windows system menu.

## Asset extraction

The optional extraction tool validates your EXE and DAT, exports all 652 archive
entries, converts supported formats, and writes typed metadata for all 36
levels:

```powershell
python tools/extract_assets.py
```

Output is written beneath `assets/`, which Git intentionally ignores because it
contains copyrighted data from your copy.

## Verification and documentation

The test suite covers archive and rendering rules, gameplay behavior, standalone
runtime imports, registered-executable validation, recovered executable tables,
the extraction pipeline, and the parity ledger.

| Document | Contents |
|---|---|
| [Architecture](docs/ARCHITECTURE.md) | Binary facts, asset layout, and port boundaries |
| [Executable analysis](docs/EXECUTABLE.md) | MZ-aware disassembly and control-flow map |
| [Gameplay reconstruction](docs/GAMEPLAY.md) | Instruction-level gameplay evidence |
| [Parity audit](docs/PARITY_AUDIT.md) | Function ledger, matches, and native qualifications |
| [Laser audit](docs/LASER_AUDIT.md) | Complete laser-shot behavior audit |
| [1.1.2 release notes](docs/RELEASE_NOTES_1.2.md) | Native XInput controller support |
| [1.1.1 release notes](docs/RELEASE_NOTES_1.1.md) | Widescreen and floor-recovery update |
| [1.0 release notes](docs/RELEASE_NOTES_1.0.md) | Initial release highlights and validation |

## Credits and license

Hocus Native is an unofficial fan project and is not affiliated with or
endorsed by Moonlite Software or Apogee Entertainment. See [CREDITS.md](CREDITS.md)
for the original-game credits, port credits, purchase links, and legal notice.

The original Hocus Native source code is available under the
[MIT License](LICENSE.md). That license does not apply to *Hocus Pocus* or any
original game content. Never commit or redistribute `HOCUS.EXE`, `HOCUS.DAT`,
`HOCUS.SAV`, extracted assets, or locally built binaries containing game data.
