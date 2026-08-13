#!/usr/bin/env python3
"""Recursively disassemble the 16-bit Hocus Pocus MZ executable.

This is deliberately conservative: it follows direct calls and control-flow
edges from the DOS entry point and known Borland main entry, but does not label
unreached bytes as code. Indirect calls are typed only from audited runtime or
driver interfaces, near jump tables are seeded only after their selector bounds
and exact extents have been verified, and game-owned callbacks are seeded only
where their address is explicitly installed by the executable.
"""

from __future__ import annotations

import argparse
import json
import struct
from collections import defaultdict, deque
from dataclasses import dataclass
from pathlib import Path

from capstone import Cs, CS_ARCH_X86, CS_MODE_16
from capstone.x86_const import X86_OP_IMM


REGISTERED_1_1_SIZE = 182_656
KNOWN_MAIN = (0x0548, 0x035A)

# Direct control-flow traversal cannot discover functions whose addresses are
# handed to DOS or a driver as data.  These entries are admitted only after the
# registration call and the complete callback body have both been audited.
# Rows are (segment, offset, registration segment, registration offset, role).
KNOWN_CALLBACK_ENTRIES = (
    (0x1392, 0x026E, 0x1392, 0x0229, "DOS INT 09h keyboard ISR"),
)
# Curated only after the dispatcher's range check, selector transform, and
# table extent have all been confirmed from the registered-v1.1 image. Each
# row is (dispatch segment, dispatch offset, table segment, table offset,
# entry count, recovered role).
KNOWN_NEAR_JUMP_TABLES = (
    (0x0548, 0x056C, 0x0548, 0x08FB, 9, "top-level state dispatch"),
    (0x0548, 0x0771, 0x0548, 0x08DD, 6, "in-game menu dispatch"),
    (0x06B8, 0x2211, 0x06B8, 0x2249, 8, "front-end action dispatch"),
    (0x06B8, 0x4674, 0x06B8, 0x487B, 7, "ending-page action dispatch"),
    (0x06B8, 0x46D0, 0x06B8, 0x486F, 6, "ending in-game menu dispatch"),
    (0x0BA5, 0x071B, 0x0BA5, 0x0910, 9, "enemy creation behaviour"),
    (0x0BA5, 0x1478, 0x0BA5, 0x1F9A, 8, "active enemy behaviour"),
    (0x0000, 0x1850, 0x0000, 0x1C43, 22, "Borland scanf conversion"),
    (0x0000, 0x2105, 0x0000, 0x24C7, 24, "Borland printf conversion"),
    (0x156D, 0x0023, 0x156D, 0x00D1, 6, "audio error text selection"),
    (0x15D4, 0x0022, 0x15D4, 0x00A9, 8, "audio error text selection"),
    (0x15D4, 0x004E, 0x15D4, 0x009F, 5, "audio-device error detail"),
    (0x15D4, 0x00D0, 0x15D4, 0x0109, 5, "audio device selection"),
    (0x15D4, 0x012D, 0x15D4, 0x0154, 5, "audio device status"),
    (0x15D4, 0x01E2, 0x15D4, 0x0220, 5, "audio device shutdown"),
    (0x1731, 0x0020, 0x1731, 0x00C7, 13, "audio error text selection"),
    (0x1830, 0x0015, 0x1830, 0x007F, 9, "audio error text selection"),
    (0x1910, 0x0018, 0x1910, 0x005D, 4, "audio error text selection"),
    (0x1953, 0x0014, 0x1953, 0x0059, 4, "audio error text selection"),
    (0x197E, 0x0022, 0x197E, 0x00B8, 11, "audio error text selection"),
    (0x197E, 0x0A9C, 0x197E, 0x0B27, 8, "MIDI mode selection"),
)

# These sites intentionally call through runtime callbacks or the selected
# sound driver's method table. Their target is data-dependent, but their type
# and ownership are known; none can reveal an unaccounted game-owned routine.
KNOWN_DYNAMIC_TRANSFERS = {
    (0x0000, 0x0222): "Borland far-exit callback",
    (0x0000, 0x0229): "Borland near-exit callback",
    (0x0000, 0x0263): "Borland far-exit callback",
    (0x0000, 0x026A): "Borland near-exit callback",
    (0x0000, 0x11E9): "Borland destructor callback table",
    (0x0000, 0x11F9): "Borland runtime termination hook",
    (0x0000, 0x1211): "Borland runtime termination hook",
    (0x0000, 0x1215): "Borland runtime termination hook",
    (0x0000, 0x17C7): "Borland formatted-input reader callback",
    (0x0000, 0x17F1): "Borland formatted-input reader callback",
    (0x0000, 0x1804): "Borland formatted-input pushback callback",
    (0x0000, 0x181A): "Borland formatted-input pushback callback",
    (0x0000, 0x1A72): "Borland formatted-input reader callback",
    (0x0000, 0x1B1B): "Borland formatted-input reader callback",
    (0x0000, 0x1B59): "Borland formatted-input pushback callback",
    (0x0000, 0x1B9A): "Borland formatted-input pushback callback",
    (0x0000, 0x1BB8): "Borland formatted-input reader callback",
    (0x0000, 0x1BD2): "Borland formatted-input computed continuation",
    (0x0000, 0x1CB9): "Borland numeric-input reader callback",
    (0x0000, 0x1CF0): "Borland numeric-input reader callback",
    (0x0000, 0x1D23): "Borland numeric-input reader callback",
    (0x0000, 0x1D46): "Borland numeric-input pushback callback",
    (0x0000, 0x1D6F): "Borland numeric-input reader callback",
    (0x0000, 0x1DAD): "Borland numeric-input reader callback",
    (0x0000, 0x1DDD): "Borland numeric-input reader callback",
    (0x0000, 0x1DF3): "Borland numeric-input pushback callback",
    (0x0000, 0x207B): "Borland formatted-output writer callback",
    (0x0000, 0x3057): "Borland interrupt trampoline callback",
    (0x0000, 0x3183): "Borland floating-point conversion vector",
    (0x0000, 0x3187): "Borland floating-point conversion vector",
    (0x0000, 0x318B): "Borland floating-point conversion vector",
    (0x0000, 0x318F): "Borland floating-point conversion vector",
    (0x144E, 0x05D3): "selected audio-driver method +0C",
    (0x144E, 0x0628): "selected audio-driver method +0C",
    (0x144E, 0x0665): "selected audio-driver method +0C",
    (0x144E, 0x0675): "selected audio-driver method +0C",
    (0x144E, 0x0685): "selected audio-driver method +0C",
    (0x144E, 0x0695): "selected audio-driver method +0C",
    (0x144E, 0x06A5): "selected audio-driver method +0C",
    (0x144E, 0x071F): "selected audio-driver method +24",
    (0x144E, 0x0750): "selected audio-driver method +28",
    (0x144E, 0x07E8): "selected audio-driver method +1C",
    (0x144E, 0x09C1): "selected audio-driver method +28",
    (0x144E, 0x0B2D): "selected audio-driver method +20",
    (0x1830, 0x0889): "audio-driver callback adapter",
}

# Borland's software-8087 encoding replaces x87 opcodes with INT 34h..3Dh
# followed by operand bytes. A conventional x86 decoder treats those operand
# bytes as instructions; the apparent far jump below is therefore not reached.
KNOWN_DECODE_ARTIFACTS = {
    (0x0000, 0x0F94): "Borland software-8087 operand stream",
}


@dataclass(frozen=True, order=True)
class Address:
    segment: int
    offset: int

    @property
    def linear(self) -> int:
        return self.segment * 16 + self.offset

    def text(self) -> str:
        return f"{self.segment:04X}:{self.offset:04X}"


@dataclass
class Instruction:
    address: Address
    size: int
    raw: str
    mnemonic: str
    operands: str
    relocation_words: list[int]


class MzImage:
    def __init__(self, path: Path):
        self.path = path
        self.file = path.read_bytes()
        if len(self.file) < 28 or self.file[:2] != b"MZ":
            raise ValueError("input is not an MZ executable")
        fields = struct.unpack_from("<14H", self.file)
        (self.magic, self.last_page_bytes, self.pages, self.relocation_count,
         self.header_paragraphs, self.min_alloc, self.max_alloc, self.ss,
         self.sp, self.checksum, self.ip, self.cs, self.relocation_offset,
         self.overlay) = fields
        self.header_size = self.header_paragraphs * 16
        if self.header_size > len(self.file):
            raise ValueError("MZ header exceeds the file")
        expected_size = ((self.pages - 1) * 512 + self.last_page_bytes
                         if self.last_page_bytes else self.pages * 512)
        if expected_size != len(self.file):
            raise ValueError(f"MZ page size is {expected_size}, file is {len(self.file)}")
        self.module = self.file[self.header_size:]
        table_end = self.relocation_offset + self.relocation_count * 4
        if table_end > self.header_size:
            raise ValueError("MZ relocation table exceeds the header")
        self.relocations: set[int] = set()
        self.relocation_addresses: list[Address] = []
        for index in range(self.relocation_count):
            offset, segment = struct.unpack_from(
                "<HH", self.file, self.relocation_offset + index * 4)
            address = Address(segment, offset)
            if address.linear + 2 > len(self.module):
                raise ValueError(f"relocation {index} is outside the load module")
            self.relocation_addresses.append(address)
            self.relocations.add(address.linear)

    def bytes_at(self, address: Address, count: int = 15) -> bytes:
        if address.linear < 0 or address.linear >= len(self.module):
            return b""
        return self.module[address.linear:address.linear + count]


class RecursiveDisassembler:
    def __init__(self, image: MzImage):
        self.image = image
        self.cs = Cs(CS_ARCH_X86, CS_MODE_16)
        self.cs.detail = True
        self.instructions: dict[Address, Instruction] = {}
        self.owners: dict[Address, Address] = {}
        self.functions: set[Address] = set()
        self.calls: dict[Address, set[Address]] = defaultdict(set)
        self.branch_targets: set[Address] = set()
        self.indirect_transfers: list[dict[str, str]] = []
        self.unresolved_indirect_transfers: list[dict[str, str]] = []

    @staticmethod
    def indirect_resolution(address: Address) -> tuple[str, str] | None:
        key = (address.segment, address.offset)
        for (dispatch_segment, dispatch_offset, _table_segment,
             _table_offset, _count, role) in KNOWN_NEAR_JUMP_TABLES:
            if key == (dispatch_segment, dispatch_offset):
                return "range-validated jump table", role
        if key in KNOWN_DYNAMIC_TRANSFERS:
            return "typed dynamic callback", KNOWN_DYNAMIC_TRANSFERS[key]
        if key in KNOWN_DECODE_ARTIFACTS:
            return "decoder artifact", KNOWN_DECODE_ARTIFACTS[key]
        return None

    def record_indirect(self, cursor: Address, mnemonic: str,
                        operands: str) -> None:
        transfer = {
            "at": cursor.text(), "kind": mnemonic, "operands": operands,
        }
        resolution = self.indirect_resolution(cursor)
        if resolution is None:
            self.unresolved_indirect_transfers.append(transfer)
        else:
            transfer["resolution"] = resolution[0]
            transfer["role"] = resolution[1]
        self.indirect_transfers.append(transfer)

    def decode(self, address: Address):
        raw = self.image.bytes_at(address)
        if not raw:
            return None
        decoded = next(self.cs.disasm(raw, address.offset, count=1), None)
        if decoded is None or decoded.size == 0:
            return None
        relocations = []
        for byte_offset in range(decoded.size - 1):
            if address.linear + byte_offset in self.image.relocations:
                relocations.append(byte_offset)
        return decoded, Instruction(
            address=address,
            size=decoded.size,
            raw=decoded.bytes.hex(),
            mnemonic=decoded.mnemonic,
            operands=decoded.op_str,
            relocation_words=relocations,
        )

    @staticmethod
    def direct_far(decoded) -> Address | None:
        raw = bytes(decoded.bytes)
        if raw and raw[0] in (0x9A, 0xEA) and len(raw) >= 5:
            offset, segment = struct.unpack_from("<HH", raw, 1)
            return Address(segment, offset)
        return None

    @staticmethod
    def direct_near(decoded, segment: int) -> Address | None:
        if not decoded.operands or decoded.operands[0].type != X86_OP_IMM:
            return None
        return Address(segment, decoded.operands[0].imm & 0xFFFF)

    def run(self, seeds: list[Address], instruction_limit: int) -> None:
        function_queue = deque(seeds)
        queued_functions = set(seeds)
        while function_queue:
            function = function_queue.popleft()
            if function in self.functions or function.linear >= len(self.image.module):
                continue
            self.functions.add(function)
            block_queue = deque([function])
            visited_blocks: set[Address] = set()
            while block_queue:
                cursor = block_queue.popleft()
                if cursor in visited_blocks:
                    continue
                visited_blocks.add(cursor)
                while cursor.linear < len(self.image.module):
                    if len(self.instructions) >= instruction_limit:
                        raise RuntimeError(f"instruction limit {instruction_limit} reached")
                    if cursor in self.instructions:
                        break
                    pair = self.decode(cursor)
                    if pair is None:
                        break
                    decoded, instruction = pair
                    self.instructions[cursor] = instruction
                    self.owners[cursor] = function
                    mnemonic = decoded.mnemonic
                    fallthrough = Address(cursor.segment,
                                          (cursor.offset + decoded.size) & 0xFFFF)

                    if mnemonic in ("call", "lcall"):
                        target = (self.direct_far(decoded) if mnemonic == "lcall"
                                  else self.direct_near(decoded, cursor.segment))
                        if target is not None and target.linear < len(self.image.module):
                            self.calls[function].add(target)
                            if target not in queued_functions:
                                queued_functions.add(target)
                                function_queue.append(target)
                        else:
                            self.record_indirect(cursor, mnemonic,
                                                 decoded.op_str)
                        cursor = fallthrough
                        continue

                    if mnemonic in ("jmp", "ljmp"):
                        target = (self.direct_far(decoded) if mnemonic == "ljmp"
                                  else self.direct_near(decoded, cursor.segment))
                        if target is not None and target.linear < len(self.image.module):
                            self.branch_targets.add(target)
                            block_queue.append(target)
                        else:
                            self.record_indirect(cursor, mnemonic,
                                                 decoded.op_str)
                        break

                    is_conditional = (mnemonic.startswith("j") or
                                      mnemonic.startswith("loop")) and mnemonic != "jmp"
                    if is_conditional:
                        target = self.direct_near(decoded, cursor.segment)
                        if target is not None and target.linear < len(self.image.module):
                            self.branch_targets.add(target)
                            block_queue.append(target)
                        cursor = fallthrough
                        continue

                    if mnemonic in ("ret", "retf", "iret", "int3", "hlt"):
                        break
                    cursor = fallthrough

    def write_listing(self, path: Path) -> None:
        by_owner: dict[Address, list[Instruction]] = defaultdict(list)
        for address, instruction in self.instructions.items():
            by_owner[self.owners[address]].append(instruction)
        with path.open("w", encoding="utf-8", newline="\n") as stream:
            stream.write("; Hocus Pocus registered v1.1 recursive disassembly\n")
            stream.write(f"; load module: {len(self.image.module)} bytes; "
                         f"relocations: {len(self.image.relocations)}\n")
            stream.write(f"; entry: {self.image.cs:04X}:{self.image.ip:04X}; "
                         f"stack: {self.image.ss:04X}:{self.image.sp:04X}\n\n")
            for function in sorted(self.functions, key=lambda item: item.linear):
                instructions = sorted(by_owner.get(function, []),
                                      key=lambda item: item.address.linear)
                stream.write(f"sub_{function.segment:04X}_{function.offset:04X}:\n")
                if not instructions:
                    stream.write("    ; seed was outside decoded control flow\n\n")
                    continue
                for instruction in instructions:
                    relocation = (" ; reloc+" + ",".join(map(str, instruction.relocation_words))
                                  if instruction.relocation_words else "")
                    stream.write(
                        f"  {instruction.address.text()}  {instruction.raw:<20} "
                        f"{instruction.mnemonic:<8} {instruction.operands}{relocation}\n")
                stream.write("\n")

    def write_index(self, path: Path) -> None:
        functions = []
        owned_count = defaultdict(int)
        for owner in self.owners.values():
            owned_count[owner] += 1
        callers: dict[Address, list[Address]] = defaultdict(list)
        for caller, targets in self.calls.items():
            for target in targets:
                callers[target].append(caller)
        for function in sorted(self.functions, key=lambda item: item.linear):
            functions.append({
                "address": function.text(),
                "linear": function.linear,
                "instruction_count": owned_count[function],
                "calls": [target.text() for target in sorted(self.calls[function])],
                "called_by": [source.text() for source in sorted(callers[function])],
            })
        payload = {
            "source": str(self.image.path.resolve()),
            "header": {
                "file_size": len(self.image.file),
                "header_size": self.image.header_size,
                "module_size": len(self.image.module),
                "entry": Address(self.image.cs, self.image.ip).text(),
                "stack": Address(self.image.ss, self.image.sp).text(),
                "relocation_count": self.image.relocation_count,
            },
            "coverage": {
                "functions": len(self.functions),
                "instructions": len(self.instructions),
                "instruction_bytes": sum(item.size for item in self.instructions.values()),
                "module_bytes": len(self.image.module),
            },
            "functions": functions,
            "indirect_transfers": self.indirect_transfers,
            "unresolved_indirect_transfers": self.unresolved_indirect_transfers,
        }
        path.write_text(json.dumps(payload, indent=2), encoding="utf-8")


def parse_address(text: str) -> Address:
    try:
        segment, offset = text.split(":", 1)
        return Address(int(segment, 16), int(offset, 16))
    except (ValueError, TypeError) as error:
        raise argparse.ArgumentTypeError("address must be hexadecimal SEGMENT:OFFSET") from error


def relocated_far_transfer_seeds(image: MzImage) -> list[Address]:
    """Find immediate far calls/jumps whose segment words DOS relocates.

    The relocation at opcode+3 makes this substantially safer than scanning for
    every byte that happens to equal 0x9A or 0xEA in mixed code/data.
    """
    seeds: set[Address] = set()
    for relocation in image.relocations:
        opcode_offset = relocation - 3
        if opcode_offset < 0 or image.module[opcode_offset] not in (0x9A, 0xEA):
            continue
        offset, segment = struct.unpack_from("<HH", image.module, opcode_offset + 1)
        target = Address(segment, offset)
        if target.linear < len(image.module):
            seeds.add(target)
    return sorted(seeds)


def validated_near_jump_table_seeds(image: MzImage) -> list[Address]:
    """Read targets from manually range-validated near jump tables."""
    seeds: set[Address] = set()
    for (_dispatch_segment, _dispatch_offset, segment, offset, count,
         _role) in KNOWN_NEAR_JUMP_TABLES:
        table = Address(segment, offset)
        raw = image.bytes_at(table, count * 2)
        if len(raw) != count * 2:
            raise ValueError(f"known jump table {table.text()} is truncated")
        for index in range(count):
            target_offset = struct.unpack_from("<H", raw, index * 2)[0]
            target = Address(segment, target_offset)
            if target.linear >= len(image.module):
                raise ValueError(
                    f"known jump table {table.text()} target {index} is outside the module")
            seeds.add(target)
    return sorted(seeds)


def validated_callback_seeds(image: MzImage) -> list[Address]:
    """Validate explicit far callback addresses passed as immediate data."""
    seeds: list[Address] = []
    for (segment, offset, registration_segment, registration_offset,
         role) in KNOWN_CALLBACK_ENTRIES:
        registration = Address(registration_segment, registration_offset)
        expected = b"\x68" + struct.pack("<H", segment) + \
                   b"\x68" + struct.pack("<H", offset)
        if image.bytes_at(registration, len(expected)) != expected:
            raise ValueError(
                f"callback registration {registration.text()} no longer "
                f"installs {segment:04X}:{offset:04X} ({role})")
        target = Address(segment, offset)
        if target.linear >= len(image.module):
            raise ValueError(f"callback target {target.text()} is outside the module")
        seeds.append(target)
    return seeds


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=root / "hocus.exe")
    parser.add_argument("--listing", type=Path,
                        default=root / "analysis" / "hocus_disassembly.asm")
    parser.add_argument("--index", type=Path,
                        default=root / "analysis" / "hocus_functions.json")
    parser.add_argument("--seed", type=parse_address, action="append", default=[])
    parser.add_argument("--instruction-limit", type=int, default=100_000)
    args = parser.parse_args()

    image = MzImage(args.exe)
    if len(image.file) != REGISTERED_1_1_SIZE:
        raise ValueError("registered version 1.1 HOCUS.EXE is required")
    relocation_seeds = relocated_far_transfer_seeds(image)
    jump_table_seeds = validated_near_jump_table_seeds(image)
    callback_seeds = validated_callback_seeds(image)
    seeds = list(dict.fromkeys([
        Address(image.cs, image.ip), Address(*KNOWN_MAIN),
        *relocation_seeds, *jump_table_seeds, *callback_seeds, *args.seed,
    ]))
    disassembly = RecursiveDisassembler(image)
    disassembly.run(seeds, args.instruction_limit)
    args.listing.parent.mkdir(parents=True, exist_ok=True)
    args.index.parent.mkdir(parents=True, exist_ok=True)
    disassembly.write_listing(args.listing)
    disassembly.write_index(args.index)
    coverage = sum(item.size for item in disassembly.instructions.values())
    print(f"decoded {len(disassembly.instructions):,} instructions in "
          f"{len(disassembly.functions):,} conservative code-entry targets")
    print(f"seeded {len(relocation_seeds):,} relocation-validated far transfers")
    print(f"seeded {len(jump_table_seeds):,} range-validated jump-table targets")
    print(f"seeded {len(callback_seeds):,} explicitly installed callback entries")
    print(f"covered {coverage:,}/{len(image.module):,} load-module bytes; "
          f"classified {len(disassembly.indirect_transfers):,} indirect transfers "
          f"({len(disassembly.unresolved_indirect_transfers):,} unresolved)")
    print(f"wrote {args.listing} and {args.index}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
