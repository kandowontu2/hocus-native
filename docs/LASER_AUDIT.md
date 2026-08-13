# Registered-v1.1 laser audit

This audit traces every game-owned registered-v1.1 path that creates, stores,
draws, fires, moves, collides, or cheats laser shots. The native default path
matches these routines; the Ctrl+Alt+F1 persistent BANANA toggle remains an
explicit native extension layered over the original one-shot cheat.

| DOS address | Recovered behavior | Native implementation and result |
|---|---|---|
| `0BA5:462B` | Level initialization clears laser inventory `CE94`. | `GameLevel` begins with zero `laser_shots`; laser inventory is not carried between levels. Match. |
| `0BA5:3DD6-3DFC` | A primary-sample type-11 pickup adds three shots, plays sound 9, makes the indicator visible, and resets its counter to five. Side samples leave the item intact. | Event type 11 performs the same add, sound, primary-sample policy, visibility, and counter reset. Match. |
| `0BA5:5379-53E7` | When Hocus is visible and inventory is nonzero, draw at most three vertical laser frames at `player X + 4` pixels and `player Y - 14 - index*14`; decrement the five-tick counter and toggle visibility at zero. | `draw_laser_indicator` and the pre-movement player snapshot implement the same cap, coordinates, visibility restriction, and cadence. Audit correction: the missing initial `-14` Y offset was restored. |
| `0BA5:56DD-5805` | Fire only when a projectile slot is free and the active count is below firepower. If inventory is nonzero, decrement it and mark the new shot as a laser. Horizontal spawn is `X +/- 8, Y + 8`; vertical spawn is `X + 4, Y`. Direction is snapshotted. Sound is 0 normally or 13 during super shot. | `spawn_projectile` matches slot/cap checks, decrement/flag order, anchors, direction, firing pose, and sound selection. Match. |
| `0BA5:5806-5F61` | All player shots use the same three direction-specific current-position wall probes, breakable-tile removal/Twink paths, solid-wall bursts, 16-pixel movement, viewport release, rendering, random trail gate, and pool accounting. Laser status changes artwork but not wall behavior. | `update_projectiles` shares those paths for normal and laser shots and selects frames 13/14 for lasers. East, west, vertical, breakable, solid, bounds, and trail paths are regression-tested. Match. |
| `0BA5:0267-0438` | Enemy-major/projectile-minor collision tests the shot point `(x,y+2)`. An ordinary shot is consumed. A nonzero laser flag bypasses remaining health, enters the death path, and leaves the projectile active so it may hit later enemy slots. The `-2` kill-all sentinel remains authoritative. | `collide_projectiles_with_enemies` and `hit_enemy` match point collision, iteration order, instant kill, piercing, active counts, spawn-marker clearing, explosions, sound 10, and sentinel handling. Match. |
| `0BA5:5CE1-5D46`, `5EF3-5F5E`, `0BA5:249E` | Horizontal and vertical laser flags select the Hocus sprite record's laser frames while retaining the shot's captured facing direction. | `draw_projectiles` selects horizontal frame 13 or vertical frame 14 and the captured east/west variant. Match. |
| `1392:044E-04E7` | BANANA's scan-code sum sets inventory to three only if it is empty, then makes the indicator visible and resets its counter to five. | `apply_cheat(laser_shots)` has the same one-shot behavior. The menu toggle additionally replenishes inventory to at least three after each update. Match plus documented native extension. |

Laser inventory has no original save-slot field. Registered `HOCUS.SAV` stores
the level-entry state used by the DOS save/restore wrappers, and the level
loader clears `CE94`; the native port therefore deliberately does not serialize
laser inventory.

Focused smoke coverage now locks pickup consumption and sound, indicator cap,
offset and blink timing, horizontal laser raster selection, ammo decrement,
instant enemy death, piercing projectile survival, persistent BANANA refill,
and the shared projectile wall/effect paths.
