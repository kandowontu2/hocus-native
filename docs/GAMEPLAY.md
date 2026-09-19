# Gameplay reconstruction notes

## Player coordinate model

The level runner begins at `0BA5:453A`. Its initialization reads `.000` through
the archive layer and transforms the start position as follows:

- `0BA5:4734-4739`: `player_x = start_x * 2`
- `0BA5:473C-4742`: `player_y = start_y * 16`

Collision later divides X by two to obtain a map column, proving each X unit is
half a 16-pixel tile (8 pixels). Y is maintained in pixels and divided by 16 to
obtain a row. E1L1 therefore starts at `(6, 912)` internally, or pixel `(48,912)`.

## Collision and walking

The pointer at `DS:7062` is the `.010` main map layer. Every collision sample
compares a byte against `0xFF`; `0xFF` is empty and any other value is solid.
The layer stride is the literal `0xF0` (240 cells).

`0BA5:617D-631B` calculates four flags for left/right movement and one-tile
step-up. An aligned 24x32 Hocus samples two rows at its side; a vertically
unaligned Hocus samples three. A blocked side can be stepped onto when the row
above and current row are empty but the row below is solid. At `0BA5:633E-6408`
Left/Right changes X by one unit (8 pixels), or changes X by one and Y by -16 for
a step-up. Changing facing direction consumes one tick before movement.

## Jump and gravity

Normal jump deltas are the 19 signed words at file `0x21D20` / `DS:2C10`:

```text
-16 -8 -8 -4 -4 -4 -2 -1 -1 0 1 1 2 4 4 4 8 8 16
```

They sum to zero and reach a cumulative -48-pixel apex. `0BA5:5FBF` selects the
table, `0BA5:6025-6040` applies one entry and calculates the current tile row,
and `0BA5:6065-608B` reverses to the matching descending entry after a ceiling
collision. The super-jump path at `0BA5:5FDD` selects the analogous 25-entry
table at `DS:2BDE`. Gravity at `0BA5:612D` advances 16 pixels when unsupported.

The native `GameLevel` state machine implements these recovered integer rules
on the runner's fixed update. Standard speed is 20 Hz; the original slower and
faster game-speed choices select their corresponding native timer intervals.

## Event layer and item dispatcher

`0BA5:39F3` is the `.012` event dispatcher. The outer loop derives the player
row and column before calling it at `0BA5:4DC6`. The nested loops at
`0BA5:3A04-452D` sample the full six-cell player footprint: columns
`center - 1` through `center + 1` on both the current row and the next row.
`0x7530` (30000) is the empty event sentinel. The native regression suite
checks the right and lower samples explicitly because omitting either makes a
visibly overlapped pickup impossible to collect.

Events below 23 index the 42-byte item table at `DS:2604`. The dispatcher reads
score at record `+36`, health at `+38`, firepower at `+39`, and behaviour type at
`+40`. The confirmed base behaviours now implemented by the native runtime are:

- score treasures increment the per-level treasure count and score;
- crystals increment the crystal count;
- ordinary and kill-field damage persist and use 20- and 10-tick immunity
  windows respectively;
- health pickups remain when health is already full;
- firepower is clamped to 10, and keys/power-up state is retained;
- wizard-note cells persist and Up opens the matching 504-byte `.002` message
  record.

On collection, `0BA5:3EDE-3EF8` writes `0x7530` to `.012` and the first byte of
the level's `.001` record to `.009`. For E1L1 that replacement background tile
is `7`. The native renderer performs the same two mutations, so collected items
visibly disappear without changing the solid `.010` layer.

The unused registered item at event 17/type 12 has a distinct two-cell path at
`0BA5:3E04-3E53`: it saves Hocus's current X/Y return coordinates and clears
both its own cell and the following cell. No registered level places event 17,
but the native dispatcher retains the recovered behavior rather than treating
it as an ordinary one-cell pickup.

## Input and projectile dispatcher

The level runner keeps the three relevant controls separate. `DS:70A9` starts
the jump path at `0BA5:5F74`, `DS:70AA` enters projectile creation at
`0BA5:56E7`, and the edge generated from the Up control at `DS:70AE` activates
wizard messages. While Up is held, `DS:CF48` also selects the upward-shot path.
The native mapping is therefore Space for jump, Ctrl for Fire, and Up for
interaction/upward aim. The two recovered view controls default to Page Up and
Page Down. When joystick control is enabled, gameplay uses the joystick path
exclusively, including buttons three and four for those view controls.
The native front end now prefers XInput across all four Windows user slots and
falls back to this recovered WinMM path. XInput maps the D-pad/left stick to
movement, A to jump/confirm, X/B/right trigger to Fire, Start to pause, B/Back
to cancel, and the shoulder buttons to the two viewport controls. Its standard
left-stick dead zone replaces DOS calibration only for that native backend.

Escape enters the recovered in-game menu instead of discarding the session.
The seven original strings map to Help, Restart, Save, Restore, Options,
Resume, and Quit to Main Menu. DOS's independent 140 Hz time counter continues
through these modal screens, but gameplay state does not; the native runner
preserves that policy and performs the recovered immediate update on return.

`0BA5:5708-57E1` scans ten projectile slots and permits no more active shots
than the current firepower value at `DS:7046`. Normal Fire is edge-triggered;
the timed super-shot state keeps the request active while Fire is held. A shot
consumes one of the limited laser charges at `DS:CE94` when available. The four
render choices are the base frame offsets selected at `0BA5:5A61-5A79` and
`0BA5:5EF3-5F0A`: normal, upward, laser, and upward laser. They decode as Hocus
sprite-record frames 11 through 14; frames 9 and 10 are the matching horizontal
and upward firing poses.

Horizontal shots advance 16 pixels per update (`CEEE += 4`, with four pixels
per stored unit); upward shots subtract 16 from their Y coordinate. The east,
west, and upward branches at `0BA5:5853`, `0BA5:5AC9`, and `0BA5:5D49` sample
the `.010` solid layer and expire on collision or at the viewport boundary.
Before the ordinary solid test, each branch compares tiles with `.001` byte 3
at `DS:6D6B`. A match is replaced by `0xFF`, producing the original breakable
wall behaviour. E1L1 uses tile `0x51` for its six breakable cells.

The player spell does not come from `BULLIT.IMG`. That 128x15 planar strip is
the five pickup-effect cells plus their pre-flipped variants. The native port
draws the original yellow/orange lightning streaks from `SPRITES.SPR`, emits
the recovered 20-slot falling spell trail, and uses `BULLIT.IMG` for the
17-tick rising pickup sparkles selected by each item type.

The type-five super-shot pickup saves the current firepower at `DS:CE86`, sets
firepower to ten, and initializes `DS:CE88` to 600. `0BA5:5806-5821` decrements
that counter in the projectile dispatcher; when it reaches one, the original
firepower is restored. Recollecting the pickup refreshes the timer without
overwriting the saved value. The native state follows the same lifecycle.

## Teleporter records

Events 23..32 select the ten four-byte records in `.003`. Each record is a pair
of 16-bit map cells: a trigger endpoint and a destination. `0BA5:3F07-3F19`
requires the event to be on the first endpoint and requires the primary player
sample, making the transition one-way even when the same event is painted at
both endpoints. Activation consumes the trigger event and replaces its
background tile just like an item.

The counter at `DS:CF06` starts at one. At `0BA5:65EA-65F5` the first 25 states
lock input for the morph-out sequence. State 25 moves Y toward the destination
by 16 pixels and X by one original X unit (8 pixels) per update. Once the exact
destination is reached, states 26..40 provide the morph-in lock before ordinary
input resumes. The native core implements that counter, movement, event
mutation, and lock. Both directions render the five original `Morph` frames
from sprite record 3; the separate 16x13 `Twinks` record 2 is used for pickup
and transition effects.

## Switch and keyhole records

Events 33..55 select the 23 22-byte records in `.004`. `0BA5:3FAC-41A7`
requires the primary player sample and the Up edge, toggles the `.009` switch
tile between `.001` bytes 1 and 2, and validates as many as four dependent map
cells. A satisfied record starts the 25-tick delay written at `DS:CE76`; its
mode then either clears the inclusive `.010` rectangle or restores its cells
from `.011`. E1L1 switch 33, for example, opens columns 145..156 on rows 52..53.

Events 81..105 select the 25 12-byte records in `.006`. The first word requires
no key, the silver key, or the gold key; byte 2 is the replacement background
tile, followed by an inclusive `.010` rectangle. `0BA5:42F7-4441` consumes a
required key, clears the keyhole event, and applies the gate mutation after ten
ticks. These record layouts and both delays are exercised directly by the
native tests.

## Movable elevators

The per-level pair at executable file `0x21C26` identifies the left and right
collision tiles of a movable elevator. Episodes one and two use `-1/-1` and
disable the branch; episode three and four levels select their own byte-valued
pair. `0BA5:6430-648D` samples two rows below Hocus and normalizes a right-tile
hit back to the pair's left cell.

While Up is held, `0BA5:64A9-6519` verifies two clear cells at the player's new
top row, moves Hocus up 16 pixels, copies the elevator pair one map row upward,
and clears its old cells. `0BA5:6526-6596` performs the symmetric operation for
Down after checking the destination elevator row. Both directions now execute
against the mutable `.010` collision layer in the native core.

## Enemy definitions, triggers, and movement behaviours

The 240-byte `.007` file is ten 24-byte enemy definitions, not tile
properties. Record word 0 selects the source sprite loaded into local sprite
slot `type + 4`; `-1` marks an unavailable type. `0BA5:05DB` reads the
extra-hit counter from word 1 (offset 2), and `0BA5:0547` reads the movement
selector from word 11 (offset 22). The adjacent word 10 is a separate timer
parameter and is zero in every used registered definition. Spawn X is stored in
four-pixel units, Y in pixels, and `0BA5:0513` installs a 20-tick morph-in timer.

The 8,000-byte `.008` file contains 250 32-byte trigger records corresponding
to events 116..365. Each record has eight enemy-type words followed by eight
spawn-cell words; `0xFFFF` terminates the valid type list. The spawn cell's
`.012` value is 106 plus the enemy type. The dispatcher queues unique cells in
32 pending slots, while `0BA5:2E9D` moves them into the eight live slots when
space is available. E1L1 trigger 116 carries three type-zero enemies at cells
`(21,57)`, `(23,57)`, and `(25,57)`.

The selector is used twice: the creation jump table at `0BA5:071B` installs
initial velocity, and the active jump table at `0BA5:1478` dispatches movement.
The registered data uses selectors 0, 1, 2, 3, 4, 5, 6, and 8:

- 0 (`0BA5:16CF`) patrols in four-pixel steps and reverses at walls or edges.
- 1 (`0BA5:195B`) patrols similarly and periodically chooses the player's side.
- 2 (`0BA5:147D`) flies with a random two-dimensional velocity, optionally
  steering horizontally toward the player from definition word 5.
- 3 (`0BA5:1555`) uses the same random flight and faces the player when it
  chooses a new vector.
- 4 jumps directly to the common animation/attack tail and does not move.
- 5 (`0BA5:1AB1`) remains stationary and faces the player.
- 6 (`0BA5:160F`) is a faster horizontal random flyer that faces the player.
- 8 bypasses the ordinary dispatcher for the Episode Three boss routine at
  `0BA5:0CE8`. It remembers its spawn X, moves four pixels every 10-19 ticks,
  returns when it strays 20 pixels, wraps a four-state direction/frame cycle,
  and can fire west from state one when Hocus is to its left. The native
  controller and renderer reproduce that loop.

Random choices are data-driven too. Startup loads archive asset 6,
`RANDOM.DAT`, as 1,000 signed words at `DS:711A`. `1392:0617` advances a
wrapping index and returns the selected word modulo its argument. The native
core now consumes the same table for initial and retargeted flight velocities.
The fourth word of each `.000` record is loaded directly at `DS:7042`; calls at
`0BA5:1794` and `0BA5:1C25` use it as that level's enemy-fire random modulus.

Enemy fire uses a separate eight-slot pool from Hocus's ten projectiles.
`0BA5:0922` allocates a slot and copies `.007` words 2, 3, 5, and 9 into its
horizontal speed, vertical speed, horizontal-homing flag, and random vertical
wobble flag. `0BA5:0A81` rejects shots outside the viewport or on a solid tile,
then applies the signed direction, optional one-unit homing, vertical speed,
and optional `random(2)-random(2)` wobble. `0BA5:0006` performs player overlap
before that movement, removes the shot on contact, and installs the same
20-tick damage immunity as enemy bodies. The native core now has the same
eight-slot movement and damage model. Sprite-header words at offsets 34/36 and
42/44 select movement and attack frame ranges. Offset 46 stores projectile
width in four-pixel VGA byte-columns; offset 48 stores height and the DOS code
adds three scanlines. Offset 50 is the launch Y offset, and offsets 52/54 select
the embedded projectile frames.
`0BA5:0922` faces the shot toward Hocus, adds the full body width when firing
east, and selector 5 delays launch by `attack_last - attack_first` ticks. The
native renderer and controller now use those exact ranges, offsets, dimensions,
embedded frames, and delay.

Both enemy bodies and enemy shots test Hocus's interior VGA columns `+2..+3`
and rows `+6..+25`, which becomes an 8x20 native-pixel box at `(x+8,y+6)`.
Hocus's own spell uses its `(x,y+2)` point for enemy overlap rather than the
full lightning artwork rectangle. Non-negative enemy extra-hit counts are
increased by `skill * 2`; negative sentinel values retain their special paths.
Near-spawn sound 14 is limited by the recovered five-tick deadline, and a
selector-four spawn emits logical sound 11.

## Recorded demo playback

Each `DEMO*.DMO` asset begins with a little-endian frame count and is exactly
`2 + count * 6` bytes. At `0BA5:54D4-5540`, the six boolean bytes are consumed
as Up/action, Left, Right, Fire, Jump, and an unused/reserved Down byte. Counts
for the registered recordings are 1,563, 1,912, 2,155, 1,903, and 1,401 frames.

The title-menu timeout at `06B8:246C-24B9` runs in two 4,500-tick stages before
returning the demo sentinel. Its caller sets episode zero and uses the Borland
`rand` LCG to choose level index 0, 2, 4, 6, or 8; the game runner loads archive
resource
`18 + level_index / 2` at `0BA5:4C77-4CAA`. This proves the five mappings are
E1L1, E1L3, E1L5, E1L7, and E1L9. The counters advance at 140 Hz, so the native
front end reproduces a roughly 32.14-second title stage, two roughly 17.86-second
credit pages, the second 32.14-second title stage, and then feeds the decoded
input records to the same 20 Hz deterministic game tick until the stored frame
count is exhausted.

`0BA5:00D3` performs player overlap damage using the selected `-4`, `-12`, or
`-16` difficulty entry at `DS:2C36` and a 20-tick immunity window. Projectile collision at
`0BA5:03A4-03C6` treats word 1 as extra hits, applies a 10-tick hit effect,
retains laser shots, and clears the spawn event only on death. `0BA5:0439`
releases ordinary enemies outside an expanded camera rectangle without clearing
that event, so they can be triggered again on a revisit.

Selector 8 stores 200 extra hits in E3L9. It uses the same projectile hit and
event-clear path as the other definitions; E3L9 still completes through its six
crystals, not through a separate boss-death transition.

E4L9's selector 99 is created automatically by `0BA5:4CAD-4CFC`, outside the
map-trigger system. Its controller at `0BA5:1019` uses the executable tables at
`DS:2BB6-2BDC` to run four stages at tile positions `(128,54)`, `(128,48)`,
`(113,42)`, and `(128,36)`. Stage health is 800, 600, 400, and 200. At each
threshold it enters a 20-tick frame-two morph before respawning at the next
position. Its normal attack cycle changes to frame one for four ticks, launches
one aimed missile, restores frame zero, and waits a `random(45)+5` interval.
The native port now reproduces the automatic spawn, staged health controller,
facing, attack loop, morph delays, and final ordinary death; a deterministic
test fires through all four stages.

## Crystal requirement and level completion

The level loader counts type-1 item events (crystals) into `DS:704A`; the item
dispatcher increments the collected count at `DS:7048`. The outer runner
compares them at `0BA5:4FE3`. Equality initializes `DS:CE8A` to 90, installs the
completion lock, and suppresses player fire, jump, gravity, and walking while
the sequence runs. The level exits when that timer reaches one at
`0BA5:690A-6911`. E1L1 has five crystal events, and the native state machine
now implements the same 90-to-1 countdown, input lock, vanished player, active
enemy purge, randomized two-Twinkle effect, and transition sound. The DOS
completion initializer does not clear Hocus's active shot pool.

Death uses the parallel counter at `DS:CE8C`. When health reaches zero,
`0BA5:4E8B` initializes it to 90; `0BA5:68FF` exits/restarts the level when the
counter reaches one. The native core locks controls, hides Hocus, and emits the
original randomized two-burst/two-Twinkle sequence before signaling the Win32
runner at the terminal tick. Enemy hits and destructible walls use the same
recovered eight-slot explosion pool; every burst contains sixteen ballistic
VGA-colour fragments with the original velocity and gravity rules.

## Sound effects

The executable exposes 16 logical sound IDs rather than embedding archive
indices at every call site. Its mapping table resolves those IDs to VOC assets
620, 624, 626, 628, 634, 630, 636, 638, 640, 642, 644, 612, 618, 622, 646, and
648. A parallel table assigns priorities 14, 13, 12, 12, 18, 17, 14, 16, 15,
15, 16, 19, 15, 14, 13, and 11 for the original eight-voice allocator.

The deterministic core emits those logical IDs for recovered call sites such
as player and enemy fire, item collection, damage, enemy death/morph, wizard
interaction, and boss transitions. The Win32 boundary converts the original
uncompressed Creative Voice blocks to unsigned eight-bit mono RIFF/WAVE images
in memory and submits every request to an eight-voice DirectSound mixer. At
capacity, the mixer follows the recovered first-eligible priority replacement
rule. The source VOC data comes from the embedded archive; no external asset or
temporary WAV file is required at runtime.
