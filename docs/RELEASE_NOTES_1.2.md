# Hocus Native 1.1.2

This maintenance release adds native XInput controller support to the
registered-v1.1 MS-DOS game *Hocus Pocus* port.

## Highlights

- Xbox-compatible controllers are detected through XInput user slots 1–4
- D-pad and left-stick navigation with the standard XInput dead zone
- A jumps and confirms; X, B, or right trigger fires
- Start opens the pause menu; B or Back cancels menus
- Left and right shoulder buttons control the viewport look offset
- XInput controllers skip the obsolete DOS calibration screen
- Legacy WinMM joysticks and their original calibration path remain supported
- XInput is loaded dynamically from the newest available Windows system API,
  preserving the self-contained build and requiring no added libraries
- Deterministic tests cover mappings, dead-zone boundaries, keyboard fallback,
  menu confirmation/cancellation, and pause edge detection

## Getting the game

This source release contains no commercial game data and no prebuilt binary.
Purchase *Hocus Pocus* from [GOG](https://www.gog.com/en/game/hocus_pocus) or
[Steam](https://store.steampowered.com/app/358290/Hocus_Pocus/), then place the
registered-v1.1 `HOCUS.EXE` and `HOCUS.DAT` in the source directory before
building. The build embeds your own `HOCUS.DAT`; the finished program validates
the adjacent registered-v1.1 `HOCUS.EXE` at runtime.

## Validation

The release passes all eight CTest targets, including standalone PE-import
validation for the statically linked Windows executable.
