# Hocus Native

Hocus Native is a from-scratch native Windows port of the registered v1.1
MS-DOS release of *Hocus Pocus*. It does not emulate DOS. The locally built
Windows executable embeds the user's registered-v1.1 `HOCUS.DAT`, while the
exact full registered-v1.1 `HOCUS.EXE` must remain beside `hocus_native.exe`
as an ownership/version check.

> [!IMPORTANT]
> This repository and its releases contain no original commercial game data.
> You must own the registered game and provide your own `HOCUS.EXE` and
> `HOCUS.DAT`. [Buy it on GOG](https://www.gog.com/en/game/hocus_pocus) or
> [buy it on Steam](https://store.steampowered.com/app/358290/Hocus_Pocus/).

Hocus Native is an unofficial fan project and is not affiliated with or
endorsed by Moonlite Software or Apogee Entertainment. See
[CREDITS.md](CREDITS.md) for the complete original-game credits, port credits,
purchase links, and legal notice.

## Current state

- The audited registered-v1.1 652-entry archive index is compiled into the
  native code and validated against `HOCUS.DAT` during the build/test pipeline.
- All 652 entries in `HOCUS.DAT` can be extracted with stable names and hashes.
- PCX, planar IMG, VGA palette, font-mask, sprite animations, story text, PCM
  VOC, and PC-speaker SFX assets are converted to PNG/TXT/WAV files by an
  offline Python tool. The recovered structures for all 36 levels are also
  emitted as typed JSON, including sparse event maps and enemy trigger groups.
- A native Win32 executable reads the archive embedded in its own Windows
  resource, composites the original piracy/Apogee/registered-title startup
  sequence, and can select and render every registered level from E1L1 through
  E4L9 with its own start, backdrop, tiles, maps, events,
  messages, switches, keyholes, teleporters, enemies, and HUD.
- The deterministic core runs recovered walking/jumping collision, lightning
  projectiles and spark trails,
  destructible walls, item/event dispatch, switches, keyed gates, one-way
  teleporter sequences, movable elevators, timed super-shot restoration, and
  ordinary enemy selectors 0 through 6, the selector-8 Episode Three boss, and
  the four-stage selector-99 Episode Four boss. Recovered phase order,
  incremental camera, attack/animation state, projectile collision branches,
  effects, and completion/death lifecycles are regression-tested. All observed
  indirect control flow is now classified; the parity audit accounts for every
  registered game-owned target and records the explicit native-service
  boundaries.
- The Win32 runner decodes the original 16 VOC effects directly in memory and
  plays gameplay sound requests through the recovered logical-ID mapping and
  eight-voice priority allocator; no converted sound files are required.
- The recovered native front end includes the original eight-entry main menu,
  episode and skill selection, ordering/story/help/credits/endings, results and
  high-score entry, the seven-entry in-game pause menu, volume/game-speed/key
  controls, optional WinMM joystick input, and all nine original save slots.
  It reads and writes the DOS registered-v1.1 990-byte `HOCUS.SAV` layout and
  makes a one-time `.bak` before the first explicit native save. Sound, music,
  joystick, speed, volume, and key settings use their original DOS fields.
- The generic menu family uses the original `HOCUS.IMG` and `BOTTOM.IMG`
  composition, proportional gradient font, radial starfield, animated cursor,
  first-letter shortcuts, persistent selections, and 20-retrace palette fades.
- The title menu runs the original title/credits/title attract cycle. The
  original Borland random-number generator selects among all five `DEMO*.DMO`
  recordings, which are decoded at 20 Hz and replayed on their recovered
  E1L1/E1L3/E1L5/E1L7/E1L9 levels; any key returns to the menu.
- CTest covers native archive/render/gameplay rules, 26 typed executable data
  tables, an exact 154-target game-function ledger, and an end-to-end temporary
  extraction of all 652 registered-v1.1 assets and 36 typed level files. Save
  tests round-trip a temporary copy; the installation's original file is never
  modified by the test suite.

Use Arrow keys and Enter through the main, restore, episode, skill, story, help,
credits, options, and results screens. During play, use Left/Right to move,
Space to jump, Ctrl to fire, Up to interact or aim upward, and Page Up/Page Down
to adjust the viewport. Escape opens the recovered pause menu, including Save,
Restore, Restart, Options, and Quit. Down lowers movable elevators. Wizard
messages are opened and dismissed with Up. Any key advances the current startup
screen while preserving the later Apogee and registered-title screens. A new
Episode 1 campaign shows the original blocking crystal tip before E1L1 begins.
During a level, Ctrl+Alt+F1 opens a paused cheat menu containing
all four cheats recovered from the registered game: FEELGOOD invincibility,
BLAKE infinite keys, QUARK permanent rapid fire, and BANANA infinite laser
shots. It also provides the native `JUMP IN MID-AIR` toggle; press and release
Jump again while airborne to start another jump. Use Up/Down or 1-5 to select,
Enter or Space to toggle, and Escape or Ctrl+Alt+F1 to close it. The original
typed cheat codes remain available as one-shot effects.
The main menu's `HIGH FPS MODE` option (shortcut `F`) adds up to 125 interpolated presentation
frames per second while retaining the selected original fixed-step game speed;
it changes visual smoothness only, not physics, enemy logic, timers, or demos.
The choice persists in `HOCUS_NATIVE.CFG`. Press `Alt+Enter` to toggle
borderless fullscreen; Alt does not open the Windows system menu.
Treasures, crystals, health, firepower, keys, and persistent damage fields now
flow through the original `.012` item dispatcher rules. Every `.007` definition
now uses its correct source-sprite, health, and selector words, while `.008`
trigger groups drive native spawning for all registered levels. Collecting a
level's crystals runs the recovered 90-tick completion lock. Enemy movement,
attack poses, and embedded projectile artwork use each definition's actual
source sprite and recovered header ranges. The original VOC effects and MIDI
music are decoded and played by native Windows services. The registered-v1.1
game is reconstructed at its logical framebuffer, input, save, timing, and
audio-dispatch boundaries. CPU-, VGA-, game-port-, and sound-card-cycle identity
is outside the boundary of a native Windows port.

## Extract the assets

From PowerShell in this directory:

```powershell
python tools/extract_assets.py
```

The command validates that the EXE and DAT match, extracts every archive entry
to `assets/raw`, converts supported formats to `assets/converted`, and writes a
machine-readable `assets/manifest.json`. The `assets` directory is intentionally
ignored by Git because it contains copyrighted data from the user's copy.
Recovered level metadata is written to `assets/converted/levels/E?L?.json`.

## Build on Windows

The project builds with MSVC or MinGW-w64:

```powershell
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
cmake --build build --target package
./build/hocus_native.exe
```

Keep `HOCUS.EXE` and `HOCUS.DAT` in this source directory while building and
running the reconstruction tests. `HOCUS.DAT` is embedded into
`hocus_native.exe` by the resource compiler. To run a packaged build, copy the
full registered-v1.1 `HOCUS.EXE` into the same folder as `hocus_native.exe`.
The runtime validates its exact 182,656-byte registered-v1.1 SHA-256 identity;
shareware, altered, or missing executables are rejected. No separately
installed C/C++ runtime or external `HOCUS.DAT` is required.
The MSVC build uses the static runtime and the MinGW build statically links its
GCC, standard-library, and threading runtimes; only Windows system DLLs remain
as imports. An existing `HOCUS.SAV` may be placed beside
the executable; otherwise a compatible blank save is created when the player
first saves. The native runtime never probes parent/source directories for a
DOS save.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for confirmed binary facts and
the reconstruction boundaries, and [docs/EXECUTABLE.md](docs/EXECUTABLE.md) for
the current MZ-aware disassembly map. [docs/GAMEPLAY.md](docs/GAMEPLAY.md)
records the instruction-level evidence behind native movement and collision.
The exhaustive discovered-function ledger and native-boundary qualifications are
in [docs/PARITY_AUDIT.md](docs/PARITY_AUDIT.md). The complete laser-path audit
is in [docs/LASER_AUDIT.md](docs/LASER_AUDIT.md).

## License

The original Hocus Native source code is available under the
[MIT License](LICENSE.md). That license does not apply to *Hocus Pocus* or any
original game content. Commercial game files and extracted assets must never
be committed or redistributed.
