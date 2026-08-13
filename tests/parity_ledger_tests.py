"""Prove that the human parity ledger covers every discovered game target."""

from __future__ import annotations

import json
import re
import sys
from collections import Counter
from pathlib import Path


GAME_SEGMENTS = {"0548", "06B8", "0B97", "0BA5"}
ADDRESS_TOKEN = re.compile(r"`([0-9A-F]{4}(?::[0-9A-F]{4})?)`")


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit(
            "usage: parity_ledger_tests.py FUNCTIONS.JSON PARITY_AUDIT.MD")

    inventory = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
    discovered = {
        function["address"]
        for function in inventory["functions"]
        if function["address"].split(":", 1)[0] in GAME_SEGMENTS
    }
    assert len(discovered) == 154, len(discovered)

    report = Path(sys.argv[2]).read_text(encoding="utf-8")
    ledger = report.split("## Game-owned code ledger", 1)[1].split(
        "## Native boundary qualifications", 1)[0]

    documented: list[str] = []
    for line in ledger.splitlines():
        if not line.startswith("|"):
            continue
        address_cell = line.split("|", 2)[1]
        tokens = ADDRESS_TOKEN.findall(address_cell)
        if not tokens:
            continue
        segment: str | None = None
        for token in tokens:
            if ":" in token:
                segment, offset = token.split(":", 1)
            else:
                assert segment is not None, (line, token)
                offset = token
            documented.append(f"{segment}:{offset}")

    counts = Counter(documented)
    duplicates = sorted(address for address, count in counts.items()
                        if count != 1)
    assert not duplicates, {"duplicate ledger targets": duplicates}
    assert set(documented) == discovered, {
        "missing from ledger": sorted(discovered - set(documented)),
        "not discovered": sorted(set(documented) - discovered),
    }
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
