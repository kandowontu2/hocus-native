#!/usr/bin/env python3
"""Lock typed data recovered directly from registered-v1.1 HOCUS.EXE."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: executable_table_tests.py EXTRACTOR HOCUS.EXE",
              file=sys.stderr)
        return 2
    extractor, executable = map(Path, sys.argv[1:])
    with tempfile.TemporaryDirectory(prefix="hocus-exe-tables-") as directory:
        output = Path(directory) / "tables.json"
        subprocess.run([
            sys.executable, str(extractor), "--exe", str(executable),
            "--output", str(output),
        ], check=True)
        report = json.loads(output.read_text(encoding="utf-8"))

    tables = report["tables"]
    if len(tables) != 26:
        raise AssertionError(f"recovered {len(tables)} tables, expected 26")
    timing = tables["timing_constants"]
    if (
            timing["timer_hz"]["address"] != "1392:0073" or
            timing["timer_hz"]["value"] != 140 or
            timing["title_attract_ticks"]["address"] != "06B8:247F" or
            timing["title_attract_ticks"]["value"] != 4500 or
            timing["credit_page_ticks"]["address"] != "06B8:4347" or
            timing["credit_page_ticks"]["value"] != 2500 or
            timing["borland_rand_multiplier_high"]["address"] !=
                "0000:172E" or
            timing["borland_rand_multiplier_high"]["value"] != 0x015A or
            timing["borland_rand_multiplier_low"]["address"] !=
                "0000:1731" or
            timing["borland_rand_multiplier_low"]["value"] != 0x4E35 or
            timing["borland_rand_increment"]["address"] != "0000:1737" or
            timing["borland_rand_increment"]["value"] != 1 or
            timing["attract_demo_modulus"]["address"] != "0548:0585" or
            timing["attract_demo_modulus"]["value"] != 5):
        raise AssertionError(f"unexpected executable timing constants: {timing}")
    if tables["game_speed_timer_ticks"] != {
            "file_offset": 0x20608, "address": "1D51:14F8",
            "values": [8, 7, 6]}:
        raise AssertionError("game-speed timer table mismatch")
    groups = {item["group"]: item for item in tables["menu_groups"]["values"]}
    if groups[0] != {
            "group": 0, "default": 0, "has_heading": False,
            "lines": [
                "Begin a new game", "Restore an old game",
                "Ordering Information~", "Instructions",
                "Legends and hints~", "Change game options", "High scores",
                "Quit - return to DOS",
            ]}:
        raise AssertionError("main-menu table mismatch")
    if (groups[4]["default"] != 1 or not groups[4]["has_heading"] or
            groups[4]["lines"] != [
                "Choose a skill level", "Easy game - good for beginners",
                "Moderate - a resonable challenge",
                "Hard - the ultimate battle!",
            ]):
        raise AssertionError("skill-menu table mismatch")
    if (groups[5]["default"] != 0 or groups[5]["has_heading"] or
            groups[5]["lines"] != [
                "How to play Hocus Pocus", "Abandon level & restart",
                "Save this game", "Restore an old game",
                "Change game options", "Back to the action!",
                "Quit to Main Menu",
            ]):
        raise AssertionError("pause-menu table mismatch")
    if tables["font_style_palette_bases"]["values"] != [
            0xC0, 0xC8, 0xD0, 0xD8, 0xE0, 0xE8, 0x70, 0x68]:
        raise AssertionError("front-end font palette ramps mismatch")
    if tables["menu_prompts"]["values"][0] != (
            "Use UP/DOWN/LETTER to move - ENTER to select"):
        raise AssertionError("menu navigation prompt mismatch")

    print("validated 26 typed executable tables and all seven menu groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
