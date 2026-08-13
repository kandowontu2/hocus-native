#!/usr/bin/env python3
"""Extract typed gameplay/global tables from registered v1.1 HOCUS.EXE."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


EXPECTED_SIZE = 182_656
EXPECTED_SHA256 = "c02d422b7ada36b2948c084074a5b96b0c712a5e8202036ad5a53f9df6c1ed5d"
HEADER_SIZE = 0x1C00
DATA_SEGMENT = 0x1D51
DATA_FILE_OFFSET = HEADER_SIZE + DATA_SEGMENT * 16


def ds_offset(file_offset: int) -> str:
    offset = file_offset - HEADER_SIZE - DATA_SEGMENT * 16
    if not 0 <= offset <= 0xFFFF:
        raise ValueError(f"file offset 0x{file_offset:X} is outside the data segment")
    return f"{DATA_SEGMENT:04X}:{offset:04X}"


def words(data: bytes, offset: int, count: int, signed: bool = False) -> list[int]:
    code = "h" if signed else "H"
    return list(struct.unpack_from(f"<{count}{code}", data, offset))


def cstring(data: bytes, offset: int) -> str:
    start = DATA_FILE_OFFSET + offset
    end = data.index(0, start)
    return data[start:end].decode("ascii")


def far_string_table(data: bytes, offset: int, count: int) -> list[str]:
    strings = []
    for index in range(count):
        pointer, segment = struct.unpack_from(
            "<2H", data, DATA_FILE_OFFSET + offset + index * 4)
        if segment != DATA_SEGMENT:
            raise ValueError(
                f"menu string pointer {segment:04X}:{pointer:04X} is not in DS")
        strings.append(cstring(data, pointer))
    return strings


def picture_records(data: bytes, offset: int, count: int) -> list[dict[str, int]]:
    records = []
    for index in range(count):
        asset, x, y = struct.unpack_from("<3H", data, offset + index * 6)
        records.append({"asset": asset, "x": x, "y": y})
    return records


def table(file_offset: int, values: object) -> dict[str, object]:
    return {"file_offset": file_offset, "address": ds_offset(file_offset),
            "values": values}


def code_word(
        data: bytes, segment: int, instruction_offset: int,
        operand_offset: int) -> dict[str, object]:
    file_offset = HEADER_SIZE + segment * 16 + instruction_offset
    value = struct.unpack_from("<H", data, file_offset + operand_offset)[0]
    return {"file_offset": file_offset,
            "address": f"{segment:04X}:{instruction_offset:04X}",
            "value": value}


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=root / "hocus.exe")
    parser.add_argument("--output", type=Path,
                        default=root / "analysis" / "executable_tables.json")
    args = parser.parse_args()

    data = args.exe.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if len(data) != EXPECTED_SIZE or digest != EXPECTED_SHA256:
        raise ValueError("registered v1.1 HOCUS.EXE hash mismatch")

    sound_mappings = []
    for index in range(16):
        voc, pc_speaker, priority = struct.unpack_from("<3H", data, 0x20680 + index * 6)
        sound_mappings.append({"logical_sound": index, "voc_asset": voc,
                               "pc_speaker_asset": pc_speaker,
                               "priority": priority})

    items = []
    for index in range(23):
        offset = 0x21714 + index * 42
        name = data[offset:offset + 36].split(b"\0", 1)[0].decode("ascii", "replace")
        score, heal, firepower, kind, padding = struct.unpack_from("<h4B", data, offset + 36)
        items.append({"index": index, "name": name, "score": score,
                      "heal": heal, "firepower": firepower, "type": kind,
                      "padding": padding})

    elevators = words(data, 0x21C26, 80, signed=True)
    result = {
        "source": {"path": str(args.exe.resolve()), "size": len(data),
                   "sha256": digest, "version": "registered-v1.1"},
        "load_model": {"header_size": HEADER_SIZE,
                       "data_segment": f"{DATA_SEGMENT:04X}"},
        "tables": {
            "timing_constants": {
                "timer_hz": code_word(data, 0x1392, 0x0073, 1),
                "title_attract_ticks": code_word(data, 0x06B8, 0x247F, 2),
                "credit_page_ticks": code_word(data, 0x06B8, 0x4347, 2),
                "borland_rand_multiplier_high": code_word(
                    data, 0x0000, 0x172E, 1),
                "borland_rand_multiplier_low": code_word(
                    data, 0x0000, 0x1731, 1),
                "borland_rand_increment": code_word(
                    data, 0x0000, 0x1737, 1),
                "attract_demo_modulus": code_word(
                    data, 0x0548, 0x0585, 1),
            },
            "game_speed_timer_ticks": table(
                DATA_FILE_OFFSET + 0x14F8,
                words(data, DATA_FILE_OFFSET + 0x14F8, 3)),
            "sound_mappings": table(0x20680, sound_mappings),
            "menu_groups": table(DATA_FILE_OFFSET + 0x1A04, [
                {"group": 0, "default": words(
                    data, DATA_FILE_OFFSET + 0x1A04, 1)[0],
                 "has_heading": False,
                 "lines": far_string_table(data, 0x1A06, 8)},
                {"group": 1, "default": words(
                    data, DATA_FILE_OFFSET + 0x1A26, 1)[0],
                 "has_heading": True,
                 "lines": far_string_table(data, 0x1A28, 5)},
                {"group": 2, "default": words(
                    data, DATA_FILE_OFFSET + 0x1A3C, 1)[0],
                 "has_heading": False,
                 "lines": far_string_table(data, 0x1A3E, 6)},
                {"group": 3, "default": words(
                    data, DATA_FILE_OFFSET + 0x1A56, 1)[0],
                 "has_heading": True,
                 "lines": far_string_table(data, 0x1A58, 10)},
                {"group": 4, "default": words(
                    data, DATA_FILE_OFFSET + 0x1A80, 1)[0],
                 "has_heading": True,
                 "lines": far_string_table(data, 0x1A82, 4)},
                {"group": 5, "default": words(
                    data, DATA_FILE_OFFSET + 0x1A92, 1)[0],
                 "has_heading": False,
                 "lines": far_string_table(data, 0x1A94, 7)},
                {"group": 7, "default": words(
                    data, DATA_FILE_OFFSET + 0x1AD0, 1)[0],
                 "has_heading": True,
                 "lines": far_string_table(data, 0x1AD2, 4)},
            ]),
            "menu_prompts": table(
                DATA_FILE_OFFSET + 0x19DC,
                far_string_table(data, 0x19DC, 8)),
            "font_style_palette_bases": table(
                DATA_FILE_OFFSET + 0x1AE6,
                words(data, DATA_FILE_OFFSET + 0x1AE6, 8)),
            "story_pictures": table(0x20A22, picture_records(data, 0x20A22, 10)),
            "ending1_pictures": table(0x20A5E, picture_records(data, 0x20A5E, 2)),
            "ending2_pictures": table(0x20A6A, picture_records(data, 0x20A6A, 2)),
            "ending3_pictures": table(0x20A76, picture_records(data, 0x20A76, 4)),
            "ending4_pictures": table(0x20A8E, picture_records(data, 0x20A8E, 2)),
            "level_time_limits": table(0x20A9A, [
                words(data, 0x20A9A + episode * 18, 9) for episode in range(4)
            ]),
            "items": table(0x21714, items),
            "tileset_assets": table(0x21ADA, [
                words(data, 0x21ADA + episode * 20, 10) for episode in range(4)
            ]),
            "level_numbers": table(0x21B2A, [
                words(data, 0x21B2A + episode * 20, 10) for episode in range(4)
            ]),
            "backdrop_assets": table(0x21B7A, [
                words(data, 0x21B7A + episode * 20, 10) for episode in range(4)
            ]),
            "music_assets": table(0x21BDE, [
                words(data, 0x21BDE + episode * 18, 9) for episode in range(4)
            ]),
            "elevator_tiles": table(0x21C26, [
                [{"left": elevators[episode * 20 + level * 2],
                  "right": elevators[episode * 20 + level * 2 + 1]}
                 for level in range(10)] for episode in range(4)
            ]),
            "boss4_x": table(0x21CC6, words(data, 0x21CC6, 4, signed=True)),
            "boss4_y": table(0x21CCE, words(data, 0x21CCE, 4, signed=True)),
            "boss4_tile_offsets": table(0x21CD6, words(data, 0x21CD6, 4, signed=True)),
            "boss4_directions": table(0x21CDE, words(data, 0x21CDE, 4, signed=True)),
            "boss4_health_thresholds": table(0x21CE6, words(data, 0x21CE6, 4, signed=True)),
            "super_jump_deltas": table(0x21CEE, words(data, 0x21CEE, 25, signed=True)),
            "normal_jump_deltas": table(0x21D20, words(data, 0x21D20, 19, signed=True)),
            "damage_knockback_deltas": table(0x21D46, words(data, 0x21D46, 5, signed=True)),
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(f"extracted {len(result['tables'])} typed executable tables to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
