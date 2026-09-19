# Registered-v1.1 function parity audit

## Result

The native port has an implementation for every observable game-owned path
reachable with the registered-v1.1 executable, archive, and save format. The
deterministic tests lock the recovered gameplay rules, front-end policies,
logical 320x200 raster, timers, saves, demos, sound dispatch, and all official
level data. DOS runtime, VGA, interrupt, and audio-driver routines terminate at
explicit native service boundaries; they are not instruction translations.

One deliberately excluded path remains: enemy selector 7 is absent from all
360 registered `.007` records and appears to perform an invalid-index memory
write. The native implementation does not reproduce undefined memory
corruption for a fabricated level. This does not affect any registered level,
demo, save, menu, or normal input path.

This report uses three distinct terms:

1. **Inventory parity** means all code-entry targets reached by direct control
   flow and every range-validated table have been counted, and every observed
   indirect transfer has an assigned type.
2. **Behavioral parity** means a recovered DOS rule has a native equivalent and
   a focused deterministic regression test.
3. **Instruction parity** is neither possible nor required for a native Win32
   rewrite of DOS, VGA, timer, input, and audio services.

`registered_v1_1_disassembly_inventory` regenerates the analysis and locks the
current conservative inventory at **649 code-entry targets**, **35,327
instructions**, and **91,942 instruction bytes**. Of the 649 targets, 158 are
labels recovered from 21 bounded near-jump tables. The game-owned segments
contain 154 targets and the runtime/platform/audio segments contain 495.
The seeds comprise **234 relocation-validated immediate far transfers**, **158
bounded jump-table targets**, and **one explicitly verified installed callback**;
the test suite locks those counts as well as the final traversal totals.
`registered_v1_1_parity_ledger` independently parses this report and the
generated JSON inventory. It fails on a missing, duplicate, or spurious
game-owned address, so the human ledger cannot drift silently from the binary.

All **67 indirect transfers are classified**:

| Class | Count | Meaning |
|---|---:|---|
| Range-validated near-jump tables | 21 | Selector transform, upper bound, table extent, and targets verified from the image |
| Typed dynamic callbacks | 45 | Borland runtime callbacks or selected audio-driver method slots |
| Decoder artifact | 1 | Operand bytes following Borland software-8087 `INT 34h..3Dh` encoding, not an executed far jump |
| Unresolved | **0** | No unassigned indirect site remains |

## Corrections made by the current audit

The instruction-led replay found and corrected these native mismatches:

- The level tick now preserves the DOS collision, pending-spawn, enemy-shot,
  enemy-update, and distant-release phase order.
- Camera state is persistent and advances by the recovered horizontal
  half-tile and vertical tile increments instead of snapping directly to
  Hocus.
- Ordinary enemies now keep the separate DOS launch countdown, attack-pose
  timer, animation delay/reset words, vertical sentinel, and hit-flash parity.
- The shared enemy tail increments and loops movement/attack frames in the DOS
  order, including the every-other-update gate and common firing RNG draw.
- Enemy projectile height is no longer inflated twice. Moving and stationary
  shots use the recovered spawn anchors, directional filter, delay, render
  snapshot, and boss zero-delay behavior.
- Enemy hit flash uses the fixed palette-`0x70` opaque mask from
  `0BA5:2896`, and high-health/boss enemies draw the recovered 78-column health
  bar.
- Hocus's lightning now probes the direction-specific current-position tile
  patterns before moving. Breakable tiles create a Twink; ordinary walls create
  the 16-particle burst at the recovered east, west, and vertical offsets.
- The complete stored-laser path is audited in `LASER_AUDIT.md`. Pickup,
  inventory/blink state, shot creation and ammo use, frames, wall/effect paths,
  instant-kill/piercing collision, sounds, and BANANA semantics match. The
  indicator's previously missing initial 14-pixel upward offset was corrected.
- The disassembler now follows all 21 validated jump tables. This exposed 156
  additional code-entry targets and 20 additional indirect sites; all are
  typed and regression-locked.
- `0B97:0001` now runs the recovered piracy, Apogee, and registered-title
  sequence with its 40/20/30-step VGA fades, 15/9/4-second holds, one-shot
  fanfare, looping title music, and startup VOC. Its `1392:0108` waits accept
  input by advancing only the current screen rather than bypassing the rest.
- `06B8:19A4` and `2259` now drive the generic DOS menu presentation: the
  original `HOCUS.IMG`/`BOTTOM.IMG` composition, proportional font metrics,
  row palette ramps, heading and `~` spacing rules, animated eight-frame cursor
  copied from the startup-hidden `BULLIT.IMG` raster on VGA page 3, 75-star
  `SINCOS.DAT`/`RANDOM.DAT` field, first-letter navigation,
  persistent group selections, and 20-retrace entry/exit fades.
- Window painting now publishes a fully composed back buffer in one `BitBlt`;
  the prior direct black clear could become visible between timer frames.
- The original Borland `srand`/`rand` LCG now selects attract demos randomly
  modulo five instead of cycling sequentially.
- Configurable viewport look-up/look-down controls, joystick buttons three and
  four, joystick-exclusive gameplay bindings, and the original two-sample
  direction confirmation are recovered.
- Native XInput support checks all four controller slots through a dynamically
  loaded Windows API, provides fixed modern-pad mappings and dead zones, skips
  legacy calibration, and falls back to the recovered WinMM joystick path.
- The crystal HUD preserves the executable's first-decimal-character rule;
  values above nine are not silently clamped to nine.
- A newly started Episode 1 campaign displays `CRYSTAL.IMG` at the recovered
  Mode-X position (48,46) and blocks for keyboard or joystick input. Restarts,
  restores, and attract demos pass the original zero flag and do not show it.
- Crystal pickup flashes now resolve indices `0x6F..0x76` through the active
  `GAMEPAL.PAL` half instead of the backdrop PCX's unused black placeholders.
- `HOCUS.DAT` is a Windows resource and runtime archive reads are memory-backed.
  The native executable nevertheless requires the exact registered-v1.1
  `HOCUS.EXE` in its own folder as an ownership/version check; it validates the
  file but never executes it or reads game assets from it.
- The full registered event family is now present: `.005` insertion records
  56..80, `.006` removal records 81..105, teleporters, switches, keyholes,
  enemy triggers, and the alternating `0BA5:2C81` pending-gate/tile updater.
- Event replay corrected final-treasure sound nine, post-dispatch health
  clamping, firepower upgrades saved during super shot, repeated completed
  switches, and side-cell gate activation.
- The gameplay raster now snapshots map, HUD, player, enemy-projectile, enemy,
  pickup, Twinkle, explosion, and spell-trail state at each DOS draw point.
  Crystal flash replaces only the map page; actors/effects remain above it.
- Enemy spawn countdowns draw randomized Twinkles only. The player teleporter's
  five-frame Morph record is no longer drawn over spawning enemies.
- Enemy creation selectors four and five now apply their exact frame/direction
  initialization, and health bars plus visual effects retain the DOS per-slot
  and inter-routine overlap order.
- Native save discovery is confined to `HOCUS.SAV` beside the executable. A
  development build cannot find or overwrite the original save in a parent
  source directory.

The asset/gameplay smoke test now locks east, west, and vertical lightning-wall
effects, breakable-wall Twinks, boss pre-action rendering, boss zero-delay
shots, the boss health raster, enemy attack/animation counters, phase order,
camera movement, sprite metadata, events, pickups, hazards, saves, results,
audio conversion, and demo playback.

## Complete discovered-code boundary

The 495 non-game targets are accounted for at the native boundary. They are
native replacements, not translations of DOS services:

| Segment(s) | Targets | DOS responsibility | Native boundary |
|---|---:|---|---|
| `0000` | 199 | Borland startup, C runtime, formatted I/O, DOS files/memory/strings, floating-point helpers | C++ runtime, filesystem, Win32 process startup |
| `05D8` | 28 | VGA modes, planes, palette, image loading/copying | indexed decoders, framebuffer composition, GDI presentation |
| `068C` | 9 | archive allocation/read/write helpers | `DatArchive` and typed decoders |
| `124A` | 4 | planar image/sprite transforms | decoded sprite frames and native blits |
| `136B` | 6 | logical sound/music facade | deterministic sound events and WinMM services |
| `1392` | 18 | timers, input, random table, waits, file wrappers | fixed tick, Win32 input/timing, `RANDOM.DAT` state |
| `1437`..`1CF4` | 231 | hardware audio, MIDI, joystick, interrupt and driver support | WinMM/MCI/joystick services or platform-inapplicable code |

Replacing these services is correct for a native port. It also means “every
DOS function has an identical native function” is not a valid certification
criterion; observable behavior at each boundary is the relevant criterion.

## Game-owned code ledger

Every one of the 154 discovered game-owned targets is included below. Labels
inside a bounded jump table are listed with their owning dispatcher.

### Startup and top-level control (18)

| Addresses | Recovered role | Audit status |
|---|---|---|
| `0548:0014` | global data/archive/audio/random initialization | Native replacement; DOS allocation and hardware setup are platform-inapplicable |
| `0548:035A`, `0570`, `05C2`, `0736`, `0745`, `0776`, `07A8`, `07FA`, `0802`, `081F`, `083E`, `0891`, `089A`, `08A4`, `08AB`, `08B2` | top-level state machine plus main/in-game dispatch tables | Recovered state flow, attract selection, level transitions, shortcuts, settings, and exit policies implemented |
| `0B97:0001` | piracy/Apogee/title startup presentation and audio sequence | Behaviorally recovered and timing-tested |

### Front end, saves, pages, and results (79)

| Addresses | Recovered role | Audit status |
|---|---|---|
| `06B8:000C`, `0080`, `0103`, `039C`, `05E9`, `071F`, `087C`, `0901`, `094B` | input polling, menu background, font width/draw primitives | Generic-menu observable behavior recovered; Win32 supplies equivalent key events and 14 ms retrace pacing |
| `06B8:0B21`, `0D99`, `0E52`, `0F3D`, `0FBC`, `10AC`, `1316`, `137D`, `1744`, `17DA`, `19A4`, `1BAE` | option strings, prompts, key definition, volume/control screens, menu composition, name input | Exact recovered strings, logical rasters, navigation, capacities, and DOS setting fields implemented |
| `06B8:1C9B`, `2018`, `2203`, `2216`, `221E`, `2226`, `222E`, `2236`, `223E`, `2246`, `2259` | high scores, bounded front-end action dispatch, generic navigation | Recovered signed score ordering, name editing, action tables, selection persistence, and attract baseline implemented |
| `06B8:24C3`, `24DC`, `24F5`, `2527`, `2540`, `2559`, `2578` | menu/screen wrappers and notice handling | Recovered wrapper flow, fades, prompts, and return policies implemented |
| `06B8:25CC`, `2B8B`, `2EE1` | save, restore, quit confirmation | Nine-slot raster, separate cursors, editing, exact 990-byte fields, and Y/N/Escape confirmation implemented |
| `06B8:2F16` | joystick-centering calibration | Center capture, one-third thresholds, fire-button choice, cancellation, and direction reset implemented through WinMM |
| `06B8:2FA8`, `3048`, `30DE`, `3187` | front-end presentation wrappers | Observable copy/fade/return behavior implemented at the native framebuffer boundary |
| `06B8:3230`, `3401`, `364D`, `3899`, `3AE5`, `3D31`, `3F7D` | story/help/order/credits/ending page viewers and footer | Both page layouts, picture records, spacing, PgUp/PgDn/Home/End controls, joystick mapping, prompts, and fades implemented |
| `06B8:407D`, `40BD`, `40FD`, `413D`, `417D`, `41BD`, `429A`, `42CB`, `44CC` | page/title/credit/ending wrappers | All registered wrapper sequences and timed credit behavior implemented |
| `06B8:4600`, `4679`, `4683`, `4689`, `469B`, `46A2`, `46A5`, `46D5`, `4707`, `476B`, `4772`, `4795`, `47B3`, `4806` | final presentation and its two bounded menu dispatch tables | Pause/finale dispatch, ending transitions, resume/restart/save/restore/options/quit policies implemented |
| `06B8:4889`, `4D76` | level results/bonuses and terminal presentation | Exact text selection/layout, signed formulas, time tables, score banking, WARP raster/audio, and transition sequence implemented |

### Level engine (57)

| Addresses | Recovered role | Audit status |
|---|---|---|
| `0BA5:0006`, `00D3`, `01B6`, `0267`, `0439`, `04D3` | enemy-shot/body/player collision, kill-all, Hocus-shot collision, release, creation | Behaviorally recovered and phase-ordered with focused tests |
| `0BA5:0720`, `0741`, `0795`, `07E8`, `0841`, `0864`, `0887`, `08B4`, `08E6` | enemy creation selectors 0..8 | Registered selectors are covered; selector 7 is dormant in all `.007` data and the native safe arc does not reproduce its apparent original out-of-bounds write |
| `0BA5:0922`, `0A81`, `0C5B`, `0CE8`, `1019`, `126C`, `147D`, `1555`, `160F`, `16CF`, `182B`, `195B`, `1AB1`, `1AD7` | enemy projectiles, health bar, E3/E4 bosses, enemy loop, active behaviors, shared attack/animation tail | Recovered registered paths are implemented and regression-tested, including RNG, delay, snapshot, attack, hit-flash, and boss timing |
| `0BA5:1FAA`, `20B1`, `2174`, `21C6` | Twinks and spell-trail pools | Pool sizes, counters, and state are implemented; VGA-plane draw timing is a native raster replacement |
| `0BA5:220F`, `2242`, `23BA`, `249E`, `25FF`, `26B3`, `27E2`, `2896` | palette fade, sprite loading, morph and directional/masked sprite variants | Six-bit palette effects, sprite variants, masks, crystal flash, and hit-flash behavior implemented in the logical framebuffer |
| `0BA5:29C5`, `2C81` | incremental camera; alternating visible tile animation and pending gate mutation | Recovered persistent camera order/increments, countdowns, tile modes, and off-screen gate progression are implemented and tested |
| `0BA5:2E9D`, `2F1C`, `2FF3` | pending enemy spawns and eight-slot/16-particle explosions | Behaviorally implemented with corrected phase and draw snapshots |
| `0BA5:3125`, `3297`, `348D` | HUD numeric primitives and HUD update | Original digit/key cells, positions, centering, flash cycle, and first-character crystal quirk implemented |
| `0BA5:3643`, `3684`, `36F0` | crystal counting and pickup sparkle allocation/update | Behaviorally implemented and tested |
| `0BA5:3751`, `37FC`, `38B6` | pause/message presentation | Exact shadow/foreground raster and any-key/two-button modal policy implemented without advancing the level |
| `0BA5:39F3` | complete registered event dispatcher | All registered event families have native paths and focused tests |
| `0BA5:453A` | level loader and monolithic fixed-step runner | Registered gameplay phase/draw order is recovered; platform timing, pause/presentation integration, and the dormant selector divergence remain |

## Native boundary qualifications

- The dormant selector-7 invalid-memory side effect is intentionally excluded,
  as described above.
- Win32, GDI, DirectSound, MCI, and WinMM replace DOS interrupts, VGA ports,
  Sound Blaster/AdLib/GUS drivers, and game-port timing. Tests certify the
  recovered game-visible policy, not CPU-cycle, DAC, or synthesizer identity.
- Window scaling and letterboxing are native additions around the default exact
  320x200 logical framebuffer. Closing the Windows title bar is likewise a
  native input outside the DOS program's input space. `Alt+Enter` toggles a
  borderless monitor-sized window, and the Win32 system-menu accelerator is
  suppressed so Alt remains available to the game's configurable controls.
- `WIDESCREEN MODE` is an explicit native main-menu addition. It cycles through
  OFF, 16:9 (356x200), 21:9 (467x200), and 32:9 (711x200), extends the
  map/entity/projectile view, camera bounds, and visible tile-mutation strip,
  extends the enemy release bound across the complete visible width while
  retaining the recovered player-contact spawn triggers,
  centres the original HUD once between solid-black side panels,
  and leaves all fixed front-end artwork at 320x200.
  It defaults to OFF, so the registered framebuffer and culling path remain
  unchanged. Its `W` accelerator and value persist in `HOCUS_NATIVE.CFG`.
- `HIGH FPS MODE` is an explicit native main-menu addition. It presents up to
  125 interpolated frames per second for camera, Hocus, enemy, and projectile
  positions while leaving the selected original fixed-step simulation cadence,
  collision, counters, RNG, audio dispatch, and demo input stream unchanged.
  Its `F` accelerator and value are native additions; the value persists in
  `HOCUS_NATIVE.CFG` without consuming opaque bytes in DOS `HOCUS.SAV`.
- `JUMP IN MID-AIR` is an explicit native cheat-menu addition. Each new Jump
  press while airborne restarts the normal jump arc; release-edge gating keeps
  a held key from restarting that arc every fixed update. While enabled, it
  also lifts the registered CF0C vertical-camera freeze during the jump table,
  allowing the unchanged incremental camera follower to track chained jumps
  both upward and downward. A bounded post-movement recovery moves Hocus the
  minimum distance upward when solid geometry overlaps his lower half, then
  clears the interrupted airborne state. Both behaviors are disabled with the
  toggle and do not alter the registered jump, collision, or camera paths when
  off.
- `CHAPTER/STAGE SELECT` is an explicit native cheat-menu addition. It can load
  any of the registered game's 36 levels while retaining the current campaign
  score; it is not presented as a recovered DOS code path.

Subject to those explicit native-port boundaries, no unresolved registered
game path or unclassified transfer remains in the audit.
