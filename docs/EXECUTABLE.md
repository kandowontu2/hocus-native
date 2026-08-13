# Executable reconstruction notes

## Load image

The registered v1.1 executable is an uncompressed 16-bit real-mode MZ image,
not a protected-mode extender executable. Its 7,168-byte header contains 1,552
relocations; the load module is 175,488 bytes. DOS begins at `0000:0000` relative
to the allocated load segment with the initial stack at `2AC7:0080`.

The entry point is Borland startup code. It captures DOS vectors, resizes the DOS
memory block, clears BSS, runs initializer records, and makes a direct far call
at `0000:014C` to `0548:035A`. The call passes the conventional startup
arguments and identifies `0548:035A` as the game-level `main` function.

Startup loads `DS` with relative segment `1D51`. The data segment therefore
begins at load-module offset `0x1D510` (file offset `0x1F110`). This is directly
confirmed by the registered archive FAT: its known file offset `0x1F1A4` maps to
`DS:0094`, cleanly separating executable code from the large global-data tail.

Within `main`, early direct calls establish useful subsystem boundaries:

- Segment `05D8` is the low-level VGA/data-decoding subsystem. `05D8:0104`
  synchronizes to vertical retrace through status port `0x3DA`; adjacent routines
  program sequencer and graphics-controller ports `0x3C4` and `0x3CE`.
- `05D8:000D` performs the initial VGA register/memory setup, while `05D8:0AFF`
  loads archive palette entries into the combined 256-colour palette buffer.
- `05D8:04F3` clears and initializes a 768-byte palette-sized buffer.
- `05D8:0A70` reads four planar image passes into the active decoded-image
  buffer; its final public name awaits confirmation from all callers.
- `06B8:24C3` configures menu group zero, composes the generic menu through
  `06B8:19A4`, and returns `06B8:2259`'s selected entry to the first major
  state dispatch.
- Runtime string/path helpers remain in segment `0000`, distinct from game code.

These labels are evidence markers, not final names. They will be promoted only
after callers, data references, and observed runtime behaviour agree.

## Registered save block

The registered executable serializes one contiguous `0x3DE`-byte region from
`DS:48CA` to `HOCUS.SAV`. The save-menu write at `06B8:2B50-2B60` passes that
exact address and length to the runtime file writer. Nine slot records are
spread across parallel arrays inside the block:

| Disk offset | DOS address | Meaning |
|---:|---:|---|
| `0x1E` | `DS:48E8` | nine zero-based episode words; `0xFFFF` is empty |
| `0x30` | `DS:48FA` | nine zero-based level words |
| `0x42` | `DS:490C` | nine skill words |
| `0x54` | `DS:491E` | nine 26-byte names (25 characters plus NUL) |
| `0x13E` | `DS:4A08` | nine existing slot-associated bytes, retained opaque |
| `0x148` | `DS:4A12` | nine little-endian 32-bit scores |

`06B8:2B1B-2B4C` copies current episode, level, skill, and score into a selected
slot before writing all 990 bytes. `06B8:2E51` rejects the `0xFFFF` sentinel;
`06B8:2E84-2ECB` restores those four values. Episode and level are proven
zero-based by the runtime's table indexing and its explicit final-level checks
for episode 3, level 8. Native tests load the untouched installation save and
round-trip a temporary copy using this layout.

## Reproducible listing

`tools/disassemble_mz.py` parses the MZ header and relocation table, converts
segment:offset addresses to load-module positions, and recursively follows
direct near/far calls and branches. It seeds immediate far-call/jump targets
whose segment operands occur in the DOS relocation table. It also follows 21
near-jump tables only after their selector transforms, range checks, exact
extents, and target offsets have been audited. It also seeds `1392:026E`, whose
address `1392:0229-0235` explicitly installs as the DOS INT 09h keyboard ISR;
ordinary CALL/JMP traversal cannot discover a callback passed as data. Dynamic
runtime/audio callbacks are typed without guessing a concrete data-dependent
destination.

```powershell
python tools/disassemble_mz.py
```

Generated listings and indexes are written under `analysis/` and ignored by
Git. The tool seeds the DOS entry, confirmed `main`, 234 relocation-validated
far transfers, 158 unique range-validated jump-table targets, and the one
explicitly installed game-owned interrupt callback.

`tools/extract_executable_tables.py` also locks 26 typed data tables, including
all seven generic-menu groups, their defaults/headings, eight footer prompts,
and the eight front-end font palette ramps.

The current pass identifies 649 conservative code-entry targets and 35,327
instructions (91,942 bytes). It observes 67 indirect transfers: 21 validated
jump tables, 45 typed Borland-runtime/audio-driver callbacks, and one
software-8087 operand-stream decoding artifact. **Zero indirect transfers are
unresolved.** The raw percentage of the complete load module is intentionally
not treated as code coverage because the module includes the `1D51` data
segment and embedded tables.

Format documentation used to cross-check archive, image, sprite, palette, and
map structures is credited to the reverse engineers listed on the
[Hocus Pocus ModdingWiki pages](https://moddingwiki.shikadi.net/wiki/Hocus_Pocus).
