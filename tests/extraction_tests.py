#!/usr/bin/env python3
"""End-to-end checks for the registered-v1.1 extraction pipeline."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 5:
        print("usage: extraction_tests.py EXTRACTOR EXE DAT MANIFEST", file=sys.stderr)
        return 2
    extractor, exe, dat, manifest = map(Path, sys.argv[1:])
    with tempfile.TemporaryDirectory(prefix="hocus-extraction-") as directory:
        output = Path(directory) / "assets"
        subprocess.run([
            sys.executable, str(extractor),
            "--exe", str(exe), "--dat", str(dat),
            "--manifest", str(manifest), "--output", str(output),
        ], check=True)

        report = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
        conversion = report["conversion"]
        if report["fat"]["entry_count"] != 652 or conversion["errors"]:
            raise AssertionError("archive inventory or conversion result mismatch")

        levels = sorted((output / "converted" / "levels").glob("E?L?.json"))
        if len(levels) != 36:
            raise AssertionError(f"converted {len(levels)} levels, expected 36")
        e1l1 = json.loads((output / "converted" / "levels" / "E1L1.json")
                         .read_text(encoding="utf-8"))
        if (e1l1["start"]["pixel_x"] != 48 or
                e1l1["start"]["pixel_y"] != 912 or
                e1l1["start"]["enemy_fire_random_modulus"] != 50):
            raise AssertionError("E1L1 start conversion mismatch")
        settings = e1l1["registered_v1_1"]
        if (settings["time_limit_value"] != 150 or
                settings["archive_entries"]["012"] != 563 or
                settings["backdrop"]["asset_index"] != 89 or
                settings["tileset"]["asset_index"] != 105 or
                settings["music"]["asset_index"] != 603):
            raise AssertionError("E1L1 executable-table conversion mismatch")
        if e1l1["known_animation_tiles"] != {
                "clear_background": 7, "switch_off": 64,
                "switch_on": 65, "breakable_main": 81}:
            raise AssertionError("E1L1 animation-tile conversion mismatch")
        if e1l1["events"]["5"] != [
                {"column": 47, "row": 35}, {"column": 95, "row": 42},
                {"column": 62, "row": 56}, {"column": 140, "row": 56},
                {"column": 18, "row": 57}]:
            raise AssertionError("E1L1 crystal event conversion mismatch")
        first_trigger = e1l1["enemy_triggers"][0]
        if first_trigger["event"] != 116 or [
                spawn["location"]["column"] for spawn in first_trigger["spawns"]
        ] != [21, 23, 25]:
            raise AssertionError("E1L1 enemy-trigger conversion mismatch")
        first_enemy = e1l1["enemy_definitions"][0]
        if ({key: first_enemy[key] for key in (
                "source_sprite_asset", "health_extra_hits",
                "projectile_pattern", "active_behavior")} != {
                    "source_sprite_asset": 4,
                    "health_extra_hits": 1,
                    "projectile_pattern": 2,
                    "active_behavior": 0,
                }):
            raise AssertionError("E1L1 enemy-definition conversion mismatch")

        e1l5 = json.loads((output / "converted" / "levels" / "E1L5.json")
                         .read_text(encoding="utf-8"))
        teleporter = e1l5["teleporters"][5]
        if teleporter != {
                "event": 28, "active": True,
                "trigger": {"cell": 11518, "column": 238, "row": 47},
                "destination": {"cell": 13014, "column": 54, "row": 54}}:
            raise AssertionError("E1L5 teleporter conversion mismatch")

        e3l1 = json.loads((output / "converted" / "levels" / "E3L1.json")
                         .read_text(encoding="utf-8"))
        if e3l1["registered_v1_1"]["elevator_tiles"] != {
                "left": 78, "right": 79}:
            raise AssertionError("E3L1 elevator-table conversion mismatch")

        e4l9 = json.loads((output / "converted" / "levels" / "E4L9.json")
                         .read_text(encoding="utf-8"))
        boss4 = e4l9["enemy_definitions"][0]
        if ({key: boss4[key] for key in (
                "source_sprite_asset", "health_extra_hits",
                "projectile_pattern", "active_behavior")} != {
                    "source_sprite_asset": 39,
                    "health_extra_hits": 800,
                    "projectile_pattern": 3,
                    "active_behavior": 99,
                }):
            raise AssertionError("E4L9 boss definition conversion mismatch")

    print("validated 652 extracted assets and 36 typed level metadata files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
