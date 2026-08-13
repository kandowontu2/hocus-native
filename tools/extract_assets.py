#!/usr/bin/env python3
"""Validate and extract the registered Hocus Pocus v1.1 data archive."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import struct
import wave
import zlib
from dataclasses import dataclass
from pathlib import Path


VERSION = "registered-v1.1"
EXE_SIZE = 182_656
FAT_OFFSET = 0x1F1A4
ASSET_COUNT = 652

# Registered-v1.1 tables at HOCUS.EXE file offsets 0x20A9A, 0x21ADA,
# 0x21B7A, and 0x21BDE. The tenth slot in each episode row is unused.
LEVEL_TIME_LIMITS = (
    (150, 300, 400, 350, 350, 350, 420, 350, 400),
    (325, 275, 300, 350, 400, 350, 400, 375, 250),
    (525, 450, 520, 475, 525, 450, 550, 450, 300),
    (480, 375, 350, 300, 400, 350, 450, 400, 180),
)
LEVEL_VISUAL_ASSETS = (
    (0, 0, 1, 1, 2, 2, 3, 3, 3),
    (4, 4, 5, 5, 6, 6, 7, 7, 7),
    (8, 8, 9, 9, 10, 10, 11, 11, 11),
    (12, 12, 13, 13, 14, 14, 15, 15, 15),
)
LEVEL_MUSIC_ASSETS = (
    (3, 3, 4, 4, 0, 0, 2, 2, 2),
    (9, 9, 7, 7, 8, 8, 1, 1, 1),
    (4, 4, 0, 0, 7, 7, 6, 6, 6),
    (2, 2, 8, 8, 9, 9, 5, 5, 5),
)
MUSIC_ASSET_NAMES = (
    "MUSIC01.MID", "MUSIC02.MID", "TITLE.MID", "MUSIC04.MID",
    "MUSIC05.MID", "MUSIC06.MID", "DUNES.MID", "SPOOKY.MID",
    "TAJMAHAL.MID", "MUSIC10.MID",
)
LEVEL_ELEVATOR_TILES = (
    ((-1, -1), (-1, -1), (-1, -1), (-1, -1), (-1, -1),
     (-1, -1), (-1, -1), (-1, -1), (-1, -1)),
    ((-1, -1), (-1, -1), (-1, -1), (-1, -1), (-1, -1),
     (-1, -1), (-1, -1), (-1, -1), (-1, -1)),
    ((78, 79), (78, 79), (62, 63), (62, 63), (40, 41),
     (40, 41), (85, 86), (85, 86), (85, 86)),
    ((58, 59), (58, 59), (122, 123), (122, 123), (70, 71),
     (70, 71), (60, 61), (60, 61), (60, 61)),
)
LEVEL_RECORD_BASES = {
    "000": 131, "001": 167, "002": 203, "003": 239,
    "004": 275, "005": 311, "006": 347, "007": 383,
    "008": 419, "009": 455, "010": 491, "011": 527,
    "012": 563,
}


@dataclass(frozen=True)
class Entry:
    index: int
    offset: int
    size: int
    name: str
    format: str
    notes: str


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def parse_fat(exe: bytes, dat_size: int) -> list[tuple[int, int]]:
    if len(exe) != EXE_SIZE or exe[:2] != b"MZ" or b"HOCUS POCUS Version 1.1" not in exe:
        raise ValueError("unsupported HOCUS.EXE; registered version 1.1 is required")
    end = FAT_OFFSET + ASSET_COUNT * 8
    if end > len(exe):
        raise ValueError("HOCUS.EXE file allocation table is truncated")
    fat = [struct.unpack_from("<II", exe, FAT_OFFSET + i * 8) for i in range(ASSET_COUNT)]
    for index, (offset, size) in enumerate(fat):
        if offset + size > dat_size:
            raise ValueError(f"archive entry {index} exceeds HOCUS.DAT")
        if index and fat[index - 1][0] + fat[index - 1][1] != offset:
            raise ValueError(f"archive entries {index - 1} and {index} are not contiguous")
    if fat[-1][0] + fat[-1][1] != dat_size:
        raise ValueError("archive FAT does not cover HOCUS.DAT exactly")
    return fat


def read_manifest(path: Path, fat: list[tuple[int, int]]) -> list[Entry]:
    with path.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    if len(rows) != ASSET_COUNT:
        raise ValueError(f"manifest has {len(rows)} rows, expected {ASSET_COUNT}")
    entries: list[Entry] = []
    used: set[str] = set()
    for expected_index, row in enumerate(rows):
        index = int(row["index"])
        offset = int(row["offset"])
        size = int(row["size"])
        name = row["name"]
        if index != expected_index or (offset, size) != fat[index]:
            raise ValueError(f"manifest/FAT mismatch at entry {expected_index}")
        if not name or name in used or Path(name).name != name or re.search(r'[<>:"/\\|?*]', name):
            raise ValueError(f"unsafe or duplicate manifest name at entry {index}: {name!r}")
        used.add(name)
        entries.append(Entry(index, offset, size, name, row["format"], row["notes"]))
    return entries


def pcx_decode(data: bytes) -> tuple[int, int, bytes, list[tuple[int, int, int, int]]]:
    if len(data) < 897 or data[0] != 0x0A or data[2:4] != b"\x01\x08":
        raise ValueError("unsupported PCX header")
    xmin, ymin, xmax, ymax = struct.unpack_from("<4H", data, 4)
    width, height = xmax - xmin + 1, ymax - ymin + 1
    planes = data[65]
    bytes_per_line = struct.unpack_from("<H", data, 66)[0]
    if planes != 1 or bytes_per_line < width or data[-769] != 0x0C:
        raise ValueError("only 8-bit one-plane PCX images are supported")
    needed = bytes_per_line * height
    decoded = bytearray()
    cursor, end = 128, len(data) - 769
    while len(decoded) < needed:
        if cursor >= end:
            raise ValueError("truncated PCX RLE data")
        value = data[cursor]
        cursor += 1
        count = 1
        if value & 0xC0 == 0xC0:
            count = value & 0x3F
            if count == 0 or cursor >= end:
                raise ValueError("invalid PCX RLE packet")
            value = data[cursor]
            cursor += 1
        decoded.extend(bytes((value,)) * count)
        if len(decoded) > needed:
            raise ValueError("PCX RLE data exceeds image dimensions")
    packed = b"".join(decoded[y * bytes_per_line:y * bytes_per_line + width] for y in range(height))
    palette_data = data[-768:]
    palette = [(palette_data[i], palette_data[i + 1], palette_data[i + 2], 255)
               for i in range(0, 768, 3)]
    return width, height, packed, palette


def vga_palette(data: bytes) -> list[tuple[int, int, int, int]]:
    if len(data) % 3:
        raise ValueError("VGA palette size is not divisible by three")
    scale = lambda value: (value * 255 + 31) // 63
    return [(scale(data[i]), scale(data[i + 1]), scale(data[i + 2]), 255)
            for i in range(0, len(data), 3)]


def img_decode(data: bytes) -> tuple[int, int, bytes]:
    if len(data) < 4:
        raise ValueError("truncated IMG")
    width4, height = struct.unpack_from("<HH", data)
    plane_size = width4 * height
    if 4 + plane_size * 4 != len(data):
        raise ValueError("IMG dimensions do not match its size")
    width = width4 * 4
    pixels = bytearray(width * height)
    for plane in range(4):
        block = data[4 + plane * plane_size:4 + (plane + 1) * plane_size]
        for y in range(height):
            for x4 in range(width4):
                pixels[y * width + x4 * 4 + plane] = block[y * width4 + x4]
    return width, height, bytes(pixels)


def font_decode(data: bytes) -> tuple[int, int, bytes, list[tuple[int, int, int, int]]]:
    if len(data) % 8:
        raise ValueError("font mask is not made of 8x8 glyphs")
    glyphs, columns = len(data) // 8, 16
    rows = (glyphs + columns - 1) // columns
    width, height = columns * 8, rows * 8
    pixels = bytearray(width * height)
    for glyph in range(glyphs):
        gx, gy = (glyph % columns) * 8, (glyph // columns) * 8
        for y in range(8):
            bits = data[glyph * 8 + y]
            for x in range(8):
                pixels[(gy + y) * width + gx + x] = 1 if bits & (1 << x) else 0
    return width, height, bytes(pixels), [(0, 0, 0, 0), (255, 255, 255, 255)]


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))


def write_png(path: Path, width: int, height: int, indices: bytes,
              palette: list[tuple[int, int, int, int]]) -> None:
    if len(indices) != width * height or not palette:
        raise ValueError("invalid indexed PNG input")
    rgba = bytearray()
    for index in indices:
        if index >= len(palette):
            raise ValueError(f"pixel index {index} exceeds palette size {len(palette)}")
        rgba.extend(palette[index])
    scanlines = b"".join(b"\0" + bytes(rgba[y * width * 4:(y + 1) * width * 4])
                         for y in range(height))
    payload = (b"\x89PNG\r\n\x1a\n" +
               png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
               png_chunk(b"IDAT", zlib.compress(scanlines, 9)) +
               png_chunk(b"IEND", b""))
    path.write_bytes(payload)


def voc_to_wav(data: bytes, path: Path) -> None:
    if not data.startswith(b"Creative Voice File\x1A") or len(data) < 26:
        raise ValueError("invalid Creative Voice File header")
    cursor = struct.unpack_from("<H", data, 20)[0]
    sample_rate: int | None = None
    pcm = bytearray()
    while cursor < len(data):
        block_type = data[cursor]
        cursor += 1
        if block_type == 0:
            break
        if cursor + 3 > len(data):
            raise ValueError("truncated VOC block header")
        size = data[cursor] | data[cursor + 1] << 8 | data[cursor + 2] << 16
        cursor += 3
        block = data[cursor:cursor + size]
        cursor += size
        if len(block) != size:
            raise ValueError("truncated VOC block")
        if block_type == 1:
            if size < 2 or block[1] != 0:
                raise ValueError("unsupported compressed VOC block")
            rate = 1_000_000 // (256 - block[0])
            if sample_rate not in (None, rate):
                raise ValueError("VOC changes sample rate")
            sample_rate = rate
            pcm.extend(block[2:])
        elif block_type == 2:
            pcm.extend(block)
        else:
            raise ValueError(f"unsupported VOC block type {block_type}")
    if sample_rate is None:
        raise ValueError("VOC contains no PCM sound-data block")
    with wave.open(str(path), "wb") as stream:
        stream.setnchannels(1)
        stream.setsampwidth(1)
        stream.setframerate(sample_rate)
        stream.writeframes(pcm)


def palette_swatch(palette: list[tuple[int, int, int, int]]) -> tuple[int, int, bytes]:
    columns, cell = 16, 8
    rows = (len(palette) + columns - 1) // columns
    width, height = columns * cell, rows * cell
    pixels = bytearray(width * height)
    for index in range(len(palette)):
        gx, gy = (index % columns) * cell, (index // columns) * cell
        for y in range(cell):
            pixels[(gy + y) * width + gx:(gy + y) * width + gx + cell] = bytes((index,)) * cell
    return width, height, bytes(pixels)


def sprite_frame_decode(data: bytes, info_offset: int, frame: int,
                        west: bool) -> tuple[int, int, bytes]:
    data_offset = struct.unpack_from("<I", data, info_offset)[0]
    width4, height = struct.unpack_from("<HH", data, info_offset + 26)
    width = width4 * 4
    pixels_offset, pixels_size = struct.unpack_from("<HH", data, info_offset + 56)
    layout_table = info_offset + (100 if west else 60)
    pixel_table = info_offset + (180 if west else 140)
    layout_cursor = data_offset + struct.unpack_from("<H", data, layout_table + frame * 2)[0]
    pixel_cursor = (data_offset + pixels_offset +
                    struct.unpack_from("<H", data, pixel_table + frame * 2)[0] * 4)
    pixel_end = data_offset + pixels_offset + pixels_size
    if not (0 < width <= 320 and height > 0 and layout_cursor < len(data) and
            pixel_cursor <= pixel_end <= len(data)):
        raise ValueError("sprite record is out of bounds")
    output = bytearray(width * height)
    pointer = 0
    mask = 0
    commands = 0
    while True:
        commands += 1
        if commands > len(data) or layout_cursor >= len(data):
            raise ValueError("unterminated sprite layout stream")
        command = data[layout_cursor]
        layout_cursor += 1
        if command == 0:
            if layout_cursor >= len(data):
                raise ValueError("truncated sprite transparency command")
            mask = data[layout_cursor]
            layout_cursor += 1
            pointer = 0
        elif command == 1:
            if layout_cursor + 2 > len(data):
                raise ValueError("truncated sprite skip command")
            pointer += struct.unpack_from("<H", data, layout_cursor)[0]
            layout_cursor += 2
        elif command == 2:
            if pointer * 4 + 3 >= 320 * height or pixel_cursor + 4 > pixel_end:
                raise ValueError("sprite layout or pixel stream is out of bounds")
            for pixel in range(4):
                logical = pointer * 4 + pixel
                x, y = logical % 320, logical // 320
                if mask & (1 << pixel) and x < width:
                    output[y * width + x] = data[pixel_cursor + pixel]
            pixel_cursor += 4
            pointer += 1
        elif command == 3:
            break
        else:
            raise ValueError(f"unknown sprite layout command {command}")
    return width, height, bytes(output)


def convert_sprite_archive(data: bytes, output: Path,
                           palette: list[tuple[int, int, int, int]]) -> dict[str, object]:
    if len(data) < 220:
        raise ValueError("truncated sprite archive")
    first_data = struct.unpack_from("<I", data)[0]
    if first_data % 220 or first_data > len(data):
        raise ValueError("invalid sprite info table size")
    output.mkdir(parents=True, exist_ok=True)
    transparent_palette = list(palette)
    transparent_palette[0] = (*transparent_palette[0][:3], 0)
    frame_offsets = (30, 32, 34, 36, 38, 40, 42, 44, 52, 54)
    metadata: list[dict[str, object]] = []
    image_count = 0
    for sprite_index in range(first_data // 220):
        info = sprite_index * 220
        name = data[info + 4:info + 26].split(b"\0", 1)[0].decode("ascii", "replace")
        width4, height = struct.unpack_from("<HH", data, info + 26)
        frame_values = [struct.unpack_from("<H", data, info + offset)[0]
                        for offset in frame_offsets]
        used_frames = [value for value in frame_values if value not in (0xFFFF,)]
        frame_count = max(used_frames, default=0) + 1
        stem = re.sub(r"[^A-Za-z0-9]+", "_", name).strip("_") or f"sprite_{sprite_index}"
        for west, direction in ((False, "east"), (True, "west")):
            for frame in range(frame_count):
                width, decoded_height, pixels = sprite_frame_decode(data, info, frame, west)
                target = output / f"{sprite_index:02d}_{stem}_{direction}_{frame:02d}.png"
                write_png(target, width, decoded_height, pixels, transparent_palette)
                image_count += 1
        metadata.append({
            "index": sprite_index,
            "name": name,
            "width": width4 * 4,
            "height": height,
            "frame_count": frame_count,
            "frame_references": frame_values,
        })
    (output / "SPRITES.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    return {"sprite_count": len(metadata), "image_count": image_count,
            "metadata": "sprites/SPRITES.json"}


def convert_text_page(data: bytes, path: Path) -> None:
    if len(data) != 1686:
        raise ValueError("text page is not 1686 bytes")
    line_count = struct.unpack_from("<H", data)[0]
    if line_count > 20:
        raise ValueError("text page line count exceeds 20")
    lines = []
    text_offset = 6 + 20 * 4
    for index in range(line_count):
        raw = data[text_offset + index * 80:text_offset + (index + 1) * 80]
        lines.append(raw.split(b"\0", 1)[0].decode("cp437", "replace").rstrip())
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def convert_pc_speaker_sfx(data: bytes, path: Path) -> None:
    if len(data) % 2:
        raise ValueError("PC speaker SFX has an odd byte count")
    frequencies = struct.unpack(f"<{len(data) // 2}h", data)
    sample_rate = 44_100
    samples_per_tick = sample_rate // 140  # exact: 315
    phase = 0.0
    pcm = bytearray()
    for frequency in frequencies[::4]:
        for _ in range(samples_per_tick):
            if frequency > 0:
                phase = (phase + frequency / sample_rate) % 1.0
                sample = 8192 if phase < 0.5 else -8192
            else:
                sample = 0
            pcm.extend(struct.pack("<h", sample))
    with wave.open(str(path), "wb") as stream:
        stream.setnchannels(1)
        stream.setsampwidth(2)
        stream.setframerate(sample_rate)
        stream.writeframes(pcm)


def level_cell(cell: int) -> dict[str, int] | None:
    if not 0 <= cell < 240 * 60:
        return None
    return {"cell": cell, "column": cell % 240, "row": cell // 240}


def convert_level_metadata(raw: Path, output: Path) -> list[dict[str, object]]:
    """Convert the evidence-backed portions of all 13-file level records."""
    output.mkdir(parents=True, exist_ok=True)
    converted: list[dict[str, object]] = []
    expected_sizes = {
        "000": 8, "001": 724, "002": 5_040, "003": 40,
        "004": 506, "005": 300, "006": 300, "007": 240,
        "008": 8_000, "009": 14_400, "010": 14_400,
        "011": 14_400, "012": 28_800,
    }
    for episode in range(1, 5):
        for number in range(1, 10):
            level_name = f"E{episode}L{number}"
            records = {
                extension: (raw / f"{level_name}.{extension}").read_bytes()
                for extension in expected_sizes
            }
            for extension, size in expected_sizes.items():
                if len(records[extension]) != size:
                    raise ValueError(
                        f"{level_name}.{extension} is {len(records[extension])} "
                        f"bytes, expected {size}")

            start_words = struct.unpack("<4H", records["000"])
            animation = records["001"]

            messages: list[dict[str, object]] = []
            message_data = records["002"]
            for index in range(10):
                base = index * 504
                column, row = struct.unpack_from("<2H", message_data, base)
                if column == 0xFFFF or row == 0xFFFF:
                    continue
                lines: list[str] = []
                for line in range(10):
                    begin = base + 4 + line * 50
                    text = message_data[begin:begin + 50].split(b"\0", 1)[0]
                    decoded = text.decode("cp437", "replace").rstrip()
                    if decoded:
                        lines.append(decoded)
                messages.append({"index": index, "column": column, "row": row,
                                 "lines": lines})

            teleporters: list[dict[str, object]] = []
            teleporter_words = struct.unpack("<20H", records["003"])
            for index in range(10):
                trigger = teleporter_words[index * 2]
                destination = teleporter_words[index * 2 + 1]
                teleporters.append({
                    "event": 23 + index,
                    "active": trigger != 0 or destination != 0,
                    "trigger": level_cell(trigger),
                    "destination": level_cell(destination),
                })

            switches: list[dict[str, object]] = []
            switch_data = records["004"]
            for index in range(23):
                base = index * 22
                dependencies = []
                for dependency in range(4):
                    cell = struct.unpack_from("<H", switch_data,
                                              base + 2 + dependency * 2)[0]
                    if cell != 0xFFFF:
                        dependencies.append({
                            "location": level_cell(cell),
                            "expected_background_tile": switch_data[base + 10 + dependency],
                        })
                first_column, first_row, last_column, last_row = struct.unpack_from(
                    "<4H", switch_data, base + 14)
                switches.append({
                    "event": 33 + index,
                    "mode": switch_data[base],
                    "dependencies": dependencies,
                    "rectangle": {
                        "first_column": first_column, "first_row": first_row,
                        "last_column": last_column, "last_row": last_row,
                    },
                })

            def keyed_records(data: bytes, first_event: int) -> list[dict[str, object]]:
                result: list[dict[str, object]] = []
                for index in range(25):
                    base = index * 12
                    requirement = struct.unpack_from("<H", data, base)[0]
                    first_column, first_row, last_column, last_row = struct.unpack_from(
                        "<4H", data, base + 4)
                    result.append({
                        "event": first_event + index,
                        "required_key": requirement,
                        "replacement_background_tile": data[base + 2],
                        "rectangle": {
                            "first_column": first_column, "first_row": first_row,
                            "last_column": last_column, "last_row": last_row,
                        },
                    })
                return result

            enemy_definitions: list[dict[str, object]] = []
            definition_data = records["007"]
            for enemy_type in range(10):
                words = list(struct.unpack_from("<12h", definition_data,
                                                enemy_type * 24))
                enemy_definitions.append({
                    "type": enemy_type,
                    "sprite_index": enemy_type + 4,
                    "available": words[0] >= 0,
                    "source_sprite_asset": words[0],
                    "health_extra_hits": words[1],
                    "projectile_pattern": words[2],
                    "motion_parameter": words[3],
                    "can_fire": words[7] != 0,
                    "active_behavior": words[11],
                    "raw_words": words,
                })

            enemy_triggers: list[dict[str, object]] = []
            trigger_data = records["008"]
            for index in range(250):
                words = struct.unpack_from("<16H", trigger_data, index * 32)
                spawns = []
                for slot, enemy_type in enumerate(words[:8]):
                    if enemy_type == 0xFFFF:
                        break
                    cell = words[8 + slot]
                    spawns.append({"type": enemy_type, "location": level_cell(cell)})
                if spawns:
                    enemy_triggers.append({"event": 116 + index, "spawns": spawns})

            event_data = records["012"]
            event_words = struct.unpack("<14400H", event_data)
            sparse_events: dict[str, list[dict[str, int]]] = {}
            for cell, event in enumerate(event_words):
                if event == 30_000:
                    continue
                sparse_events.setdefault(str(event), []).append({
                    "column": cell % 240, "row": cell // 240,
                })

            metadata = {
                "level": level_name,
                "registered_v1_1": {
                    "linear_level_index": (episode - 1) * 9 + number - 1,
                    "time_limit_value": LEVEL_TIME_LIMITS[episode - 1][number - 1],
                    "archive_entries": {
                        extension: base + (episode - 1) * 9 + number - 1
                        for extension, base in LEVEL_RECORD_BASES.items()
                    },
                    "backdrop": {
                        "asset_index": 89 + LEVEL_VISUAL_ASSETS[episode - 1][number - 1],
                        "name": f"BACK{LEVEL_VISUAL_ASSETS[episode - 1][number - 1] + 1:02}.PCX",
                    },
                    "tileset": {
                        "asset_index": 105 + LEVEL_VISUAL_ASSETS[episode - 1][number - 1],
                        "name": f"TILES{LEVEL_VISUAL_ASSETS[episode - 1][number - 1] + 1:02}.PCX",
                    },
                    "music": {
                        "asset_index": 600 + LEVEL_MUSIC_ASSETS[episode - 1][number - 1],
                        "table_value": LEVEL_MUSIC_ASSETS[episode - 1][number - 1],
                        "name": MUSIC_ASSET_NAMES[
                            LEVEL_MUSIC_ASSETS[episode - 1][number - 1]],
                    },
                    "elevator_tiles": {
                        "left": LEVEL_ELEVATOR_TILES[episode - 1][number - 1][0],
                        "right": LEVEL_ELEVATOR_TILES[episode - 1][number - 1][1],
                    },
                },
                "dimensions": {"columns": 240, "rows": 60, "tile_pixels": 16},
                "start": {
                    "unknown_word_0": start_words[0],
                    "column": start_words[1], "row": start_words[2],
                    "pixel_x": start_words[1] * 16,
                    "pixel_y": start_words[2] * 16,
                    "enemy_fire_random_modulus": start_words[3],
                },
                "known_animation_tiles": {
                    "clear_background": animation[0],
                    "switch_off": animation[1],
                    "switch_on": animation[2],
                    "breakable_main": animation[3],
                },
                "messages": messages,
                "teleporters": teleporters,
                "switches": switches,
                "inserts": keyed_records(records["005"], 56),
                "keyholes": keyed_records(records["006"], 81),
                "enemy_definitions": enemy_definitions,
                "enemy_triggers": enemy_triggers,
                "events": sparse_events,
                "raw_layers": {
                    "background": f"{level_name}.009",
                    "main": f"{level_name}.010",
                    "foreground": f"{level_name}.011",
                },
            }
            target = output / f"{level_name}.json"
            target.write_text(json.dumps(metadata, indent=2), encoding="utf-8")
            converted.append({"source": f"{level_name}.000-.012",
                              "output": f"levels/{target.name}"})
    return converted


def convert_assets(raw: Path, converted: Path, entries: list[Entry]) -> dict[str, object]:
    converted.mkdir(parents=True, exist_ok=True)
    game_palette = vga_palette((raw / "GAMEPAL.PAL").read_bytes() +
                               (raw / "MENUPAL.PAL").read_bytes())
    _, _, _, title_palette = pcx_decode((raw / "TITLE.PCX").read_bytes())
    results: dict[str, object] = {"converted": [], "skipped": [], "errors": []}
    for entry in entries:
        source = raw / entry.name
        target: Path | None = None
        try:
            suffix = source.suffix.upper()
            if suffix == ".PCX":
                width, height, pixels, palette = pcx_decode(source.read_bytes())
                target = converted / f"{source.stem}.png"
                write_png(target, width, height, pixels, palette)
            elif suffix == ".IMG":
                width, height, pixels = img_decode(source.read_bytes())
                palette = title_palette if entry.name == "REGIST.IMG" else game_palette
                target = converted / f"{source.stem}.png"
                write_png(target, width, height, pixels, palette)
            elif suffix == ".MSK":
                width, height, pixels, palette = font_decode(source.read_bytes())
                target = converted / f"{source.stem}.png"
                write_png(target, width, height, pixels, palette)
            elif suffix == ".PAL":
                palette = vga_palette(source.read_bytes())
                width, height, pixels = palette_swatch(palette)
                target = converted / f"{source.stem}.png"
                write_png(target, width, height, pixels, palette)
            elif suffix == ".VOC":
                target = converted / f"{source.stem}.wav"
                voc_to_wav(source.read_bytes(), target)
            elif suffix == ".SFX":
                target = converted / f"{source.stem}_pcspeaker.wav"
                convert_pc_speaker_sfx(source.read_bytes(), target)
            elif suffix == ".PAG":
                target = converted / f"{source.stem}.txt"
                convert_text_page(source.read_bytes(), target)
            elif suffix == ".SPR":
                target = converted / "sprites"
                sprite_result = convert_sprite_archive(source.read_bytes(), target,
                                                       game_palette)
                results["converted"].append({"source": entry.name,
                                             "output": str(target.name),
                                             **sprite_result})
                continue
            else:
                results["skipped"].append(entry.name)
                continue
            results["converted"].append({"source": entry.name, "output": target.name})
        except Exception as error:  # retain all raw assets even when a decoder is incomplete
            if target and target.exists():
                target.unlink()
            results["errors"].append({"source": entry.name, "error": str(error)})
    results["converted"].extend(
        convert_level_metadata(raw, converted / "levels"))
    return results


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=root / "hocus.exe")
    parser.add_argument("--dat", type=Path, default=root / "hocus.dat")
    parser.add_argument("--manifest", type=Path,
                        default=root / "manifests" / "hocus_registered_v1.1.csv")
    parser.add_argument("--output", type=Path, default=root / "assets")
    parser.add_argument("--raw-only", action="store_true")
    args = parser.parse_args()

    exe = args.exe.read_bytes()
    dat = args.dat.read_bytes()
    fat = parse_fat(exe, len(dat))
    entries = read_manifest(args.manifest, fat)
    raw = args.output / "raw"
    raw.mkdir(parents=True, exist_ok=True)
    inventory: list[dict[str, object]] = []
    for entry in entries:
        payload = dat[entry.offset:entry.offset + entry.size]
        (raw / entry.name).write_bytes(payload)
        inventory.append({
            "index": entry.index,
            "offset": entry.offset,
            "size": entry.size,
            "name": entry.name,
            "format": entry.format,
            "notes": entry.notes,
            "sha256": sha256(payload),
        })

    conversion = None if args.raw_only else convert_assets(raw, args.output / "converted", entries)
    report = {
        "version": VERSION,
        "sources": {
            "exe": {"path": str(args.exe.resolve()), "size": len(exe), "sha256": sha256(exe)},
            "dat": {"path": str(args.dat.resolve()), "size": len(dat), "sha256": sha256(dat)},
        },
        "fat": {"exe_offset": FAT_OFFSET, "entry_count": len(entries), "coverage": len(dat)},
        "assets": inventory,
        "conversion": conversion,
    }
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "manifest.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"validated and extracted {len(entries)} assets ({len(dat):,} bytes) to {raw}")
    if conversion is not None:
        print(f"converted {len(conversion['converted'])} assets; "
              f"{len(conversion['errors'])} conversion errors; "
              f"{len(conversion['skipped'])} formats retained as raw")
        for error in conversion["errors"]:
            print(f"  warning: {error['source']}: {error['error']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
