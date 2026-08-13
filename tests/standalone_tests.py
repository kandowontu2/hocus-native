#!/usr/bin/env python3
"""Reject redistributable C/C++ runtime DLL imports in the packaged PE."""

from __future__ import annotations

import fnmatch
import struct
import sys
from pathlib import Path


def unpack_from(fmt: str, data: bytes, offset: int) -> tuple[int, ...]:
    size = struct.calcsize(fmt)
    if offset < 0 or offset + size > len(data):
        raise ValueError("truncated PE structure")
    return struct.unpack_from(fmt, data, offset)


def pe_imports(path: Path) -> list[str]:
    data = path.read_bytes()
    if data[:2] != b"MZ":
        raise ValueError("missing MZ signature")
    (pe_offset,) = unpack_from("<I", data, 0x3C)
    if data[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise ValueError("missing PE signature")

    file_header = pe_offset + 4
    _, section_count, _, _, _, optional_size, _ = unpack_from(
        "<HHIIIHH", data, file_header
    )
    optional = file_header + 20
    (magic,) = unpack_from("<H", data, optional)
    if magic == 0x10B:
        directory_count_offset = optional + 92
        directories = optional + 96
    elif magic == 0x20B:
        directory_count_offset = optional + 108
        directories = optional + 112
    else:
        raise ValueError(f"unsupported PE optional-header magic 0x{magic:04X}")

    (directory_count,) = unpack_from("<I", data, directory_count_offset)
    if directory_count < 2:
        return []
    import_rva, _ = unpack_from("<II", data, directories + 8)
    if import_rva == 0:
        return []

    sections: list[tuple[int, int, int]] = []
    section_table = optional + optional_size
    for index in range(section_count):
        entry = section_table + index * 40
        virtual_size, virtual_address, raw_size, raw_offset = unpack_from(
            "<IIII", data, entry + 8
        )
        sections.append(
            (virtual_address, max(virtual_size, raw_size), raw_offset)
        )

    def rva_offset(rva: int) -> int:
        for virtual_address, span, raw_offset in sections:
            if virtual_address <= rva < virtual_address + span:
                return raw_offset + rva - virtual_address
        raise ValueError(f"RVA 0x{rva:X} is outside every section")

    imports: list[str] = []
    descriptor = rva_offset(import_rva)
    while True:
        fields = unpack_from("<IIIII", data, descriptor)
        if not any(fields):
            break
        name_offset = rva_offset(fields[3])
        name_end = data.find(b"\0", name_offset)
        if name_end < 0:
            raise ValueError("unterminated import-library name")
        imports.append(data[name_offset:name_end].decode("ascii"))
        descriptor += 20
    return imports


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: standalone_tests.py <windows-executable>", file=sys.stderr)
        return 2

    executable = Path(sys.argv[1])
    imports = pe_imports(executable)
    banned_patterns = (
        "libgcc_s_*.dll",
        "libstdc++-6.dll",
        "libwinpthread-1.dll",
        "vcruntime*.dll",
        "msvcp*.dll",
        "concrt*.dll",
        "clang_rt*.dll",
    )
    redistributables = [
        name
        for name in imports
        if any(
            fnmatch.fnmatch(name.lower(), pattern)
            for pattern in banned_patterns
        )
    ]
    print("PE imports: " + (", ".join(imports) if imports else "<none>"))
    if redistributables:
        print(
            "non-system runtime imports: " + ", ".join(redistributables),
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"standalone validation failed: {error}", file=sys.stderr)
        raise SystemExit(1)
