"""Reproduce and lock the registered-v1.1 function inventory."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path


EXPECTED_SEGMENTS = {
    "0000": 199,
    "0548": 17,
    "05D8": 28,
    "068C": 9,
    "06B8": 79,
    "0B97": 1,
    "0BA5": 57,
    "124A": 4,
    "136B": 6,
    "1392": 18,
    "1437": 1,
    "1439": 1,
    "143F": 2,
    "144E": 18,
    "1505": 12,
    "156D": 14,
    "15D4": 24,
    "161B": 8,
    "1711": 4,
    "1731": 46,
    "1830": 38,
    "1910": 14,
    "1953": 8,
    "197E": 32,
    "1A3D": 2,
    "1A4B": 4,
    "1CF4": 3,
}

EXPECTED_CREATION_TARGETS = {
    "0BA5:0720", "0BA5:0741", "0BA5:0795", "0BA5:07E8",
    "0BA5:0841", "0BA5:0864", "0BA5:0887", "0BA5:08B4",
    "0BA5:08E6",
}
EXPECTED_ACTIVE_TARGETS = {
    "0BA5:147D", "0BA5:1555", "0BA5:160F", "0BA5:16CF",
    "0BA5:182B", "0BA5:195B", "0BA5:1AB1", "0BA5:1AD7",
}

# Every game-owned entry target is enumerated, rather than checking only the
# segment totals.  This is the executable side of the function-by-function
# parity ledger: a newly discovered target cannot silently inherit the status
# of a neighbouring routine.
EXPECTED_GAME_OFFSETS = {
    "0548": """
        0014 035A 0570 05C2 0736 0745 0776 07A8 07FA 0802 081F 083E
        0891 089A 08A4 08AB 08B2
    """.split(),
    "06B8": """
        000C 0080 0103 039C 05E9 071F 087C 0901 094B 0B21 0D99 0E52
        0F3D 0FBC 10AC 1316 137D 1744 17DA 19A4 1BAE 1C9B 2018 2203
        2216 221E 2226 222E 2236 223E 2246 2259 24C3 24DC 24F5 2527
        2540 2559 2578 25CC 2B8B 2EE1 2F16 2FA8 3048 30DE 3187 3230
        3401 364D 3899 3AE5 3D31 3F7D 407D 40BD 40FD 413D 417D 41BD
        429A 42CB 44CC 4600 4679 4683 4689 469B 46A2 46A5 46D5 4707
        476B 4772 4795 47B3 4806 4889 4D76
    """.split(),
    "0B97": ["0001"],
    "0BA5": """
        0006 00D3 01B6 0267 0439 04D3 0720 0741 0795 07E8 0841 0864
        0887 08B4 08E6 0922 0A81 0C5B 0CE8 1019 126C 147D 1555 160F
        16CF 182B 195B 1AB1 1AD7 1FAA 20B1 2174 21C6 220F 2242 23BA
        249E 25FF 26B3 27E2 2896 29C5 2C81 2E9D 2F1C 2FF3 3125 3297
        348D 3643 3684 36F0 3751 37FC 38B6 39F3 453A
    """.split(),
}

NATIVE_BOUNDARY_SEGMENTS = set(EXPECTED_SEGMENTS) - set(EXPECTED_GAME_OFFSETS)


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: disassembly_tests.py DISASSEMBLER HOCUS.EXE")
    disassembler = Path(sys.argv[1]).resolve()
    executable = Path(sys.argv[2]).resolve()

    with tempfile.TemporaryDirectory(prefix="hocus-disassembly-") as directory:
        temporary = Path(directory)
        listing = temporary / "hocus.asm"
        index = temporary / "functions.json"
        completed = subprocess.run(
            [sys.executable, str(disassembler), "--exe", str(executable),
             "--listing", str(listing), "--index", str(index)],
            check=True, capture_output=True, text=True,
        )
        assert "seeded 234 relocation-validated far transfers" in completed.stdout
        assert "seeded 158 range-validated jump-table targets" in completed.stdout
        assert "seeded 1 explicitly installed callback entries" in completed.stdout
        data = json.loads(index.read_text(encoding="utf-8"))

    coverage = data["coverage"]
    assert coverage == {
        "functions": 649,
        "instructions": 35327,
        "instruction_bytes": 91942,
        "module_bytes": 175488,
    }, coverage
    assert len(data["indirect_transfers"]) == 67
    assert data["unresolved_indirect_transfers"] == []
    resolutions = Counter(
        transfer["resolution"] for transfer in data["indirect_transfers"])
    assert resolutions == {
        "range-validated jump table": 21,
        "typed dynamic callback": 45,
        "decoder artifact": 1,
    }, resolutions

    addresses = {function["address"] for function in data["functions"]}
    segments = Counter(address.split(":", 1)[0] for address in addresses)
    assert dict(sorted(segments.items())) == EXPECTED_SEGMENTS, segments
    assert EXPECTED_CREATION_TARGETS <= addresses
    assert EXPECTED_ACTIVE_TARGETS <= addresses
    assert "1392:026E" in addresses

    expected_game = {
        f"{segment}:{offset}"
        for segment, offsets in EXPECTED_GAME_OFFSETS.items()
        for offset in offsets
    }
    discovered_game = {
        address for address in addresses
        if address.split(":", 1)[0] in EXPECTED_GAME_OFFSETS
    }
    assert discovered_game == expected_game, {
        "missing": sorted(expected_game - discovered_game),
        "unexpected": sorted(discovered_game - expected_game),
    }
    assert len(expected_game) == 154

    discovered_boundary = addresses - discovered_game
    assert all(address.split(":", 1)[0] in NATIVE_BOUNDARY_SEGMENTS
               for address in discovered_boundary)
    assert len(discovered_boundary) == 495
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
