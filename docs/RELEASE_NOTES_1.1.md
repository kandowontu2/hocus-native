# Hocus Native 1.1.1

The first feature update to the from-scratch native Windows port of the
registered-v1.1 MS-DOS game *Hocus Pocus*, including widescreen corrections
from the superseded 1.1.0 release.

## Highlights

- New main-menu `WIDESCREEN MODE` with `OFF`, `16:9`, `21:9`, and `32:9`
  settings
- Real additional gameplay rendering at 320x200, 356x200, 467x200, or 711x200;
  the game image is never horizontally stretched
- Expanded camera, map, entity, enemy-shot, Hocus-projectile, and visible
  tile-mutation bounds; opened doors no longer remain pending at ultrawide edges
- Enemy release bounds now cover the complete expanded viewport, preventing
  visible 21:9 and 32:9 enemies from being discarded and repeatedly respawned
- Enemy encounters retain their original player-contact trigger timing in every
  aspect ratio; added side columns never activate dormant encounters early
- Original 320-pixel HUD centred exactly once between solid-black side panels
- Original menus, startup screens, story pages, and other fixed artwork remain
  unchanged at 320x200
- Monitor-work-area-aware window sizing for ultrawide modes
- Widescreen selection persists in `HOCUS_NATIVE.CFG`; the earlier boolean
  `widescreen_mode=1` value migrates automatically to `16:9`
- `JUMP IN MID-AIR` now follows Hocus vertically throughout chained jumps and
  performs a bounded minimum-distance floor eject if his lower half becomes
  embedded in solid geometry
- The registered 320x200 renderer, collision, and camera paths remain unchanged
  when the native options are disabled

## Getting the game

This source release contains no commercial game data and no prebuilt binary.
Purchase *Hocus Pocus* from [GOG](https://www.gog.com/en/game/hocus_pocus) or
[Steam](https://store.steampowered.com/app/358290/Hocus_Pocus/), then place the
registered-v1.1 `HOCUS.EXE` and `HOCUS.DAT` in the source directory before
building. The build embeds your own `HOCUS.DAT`; the finished program validates
the adjacent registered-v1.1 `HOCUS.EXE` at runtime.

## Validation

The release passes all eight CTest targets. New deterministic regressions cover
all three expanded aspect ratios, additional side-world pixels, centred HUD
composition, persistent settings, the silver `W` accelerator, cheat-enabled
floor recovery, and the unchanged cheat-disabled collision path.
