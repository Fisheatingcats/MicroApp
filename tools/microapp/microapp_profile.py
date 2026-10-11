"""Explicit board geometry for the existing fixed-address native package format."""
from __future__ import annotations

import json
import re
from pathlib import Path


def load_profile(path: Path) -> dict:
    values = json.loads(path.read_text(encoding="utf-8"))
    required = {"board_id", "slot_base", "slot_size", "image_offset", "ram_base", "ram_size",
                "abi_major", "abi_minor", "host_version"}
    if not isinstance(values, dict) or required - values.keys():
        raise ValueError("profile requires board geometry and host ABI/version")
    board = values.get("board_id")
    if not isinstance(board, str) or not board or len(board.encode("ascii")) >= 32:
        raise ValueError("board_id must be 1..31 ASCII bytes")
    geometry = {}
    for key in ("slot_base", "slot_size", "image_offset", "ram_base", "ram_size"):
        raw = values[key]
        value = int(raw, 0) if isinstance(raw, str) else raw
        if type(value) is not int or not 0 <= value <= 0xFFFFFFFF:
            raise ValueError(f"invalid {key}")
        geometry[key] = value
    if (geometry["image_offset"] < 512 or geometry["image_offset"] % 4 or
            geometry["slot_base"] % 4 or geometry["ram_base"] % 4 or
            geometry["image_offset"] >= geometry["slot_size"] or
            geometry["slot_base"] + geometry["slot_size"] > 0x100000000 or
            geometry["ram_base"] + geometry["ram_size"] > 0x100000000):
        raise ValueError("invalid native memory geometry")
    for key in ("abi_major", "abi_minor", "host_version"):
        raw = values[key]
        number = int(raw, 0) if isinstance(raw, str) else raw
        if type(number) is not int or not 0 <= number <= 0xFFFFFFFF:
            raise ValueError(f"invalid {key}")
        geometry[key] = number
    permissions = values.get("permissions", {})
    if not isinstance(permissions, dict):
        raise ValueError("permissions must map names to unique single-bit values")
    bits = set()
    for name, bit in permissions.items():
        if (not isinstance(name, str) or not name or type(bit) is not int or
                not 0 < bit <= 0x80000000 or bit & (bit - 1) or bit in bits):
            raise ValueError("permissions must map names to unique single-bit values")
        bits.add(bit)
    defaults = {"entry": "microapp_main", **{key: "__microapp_" + key for key in
                ("text_start", "text_size", "data_load", "data_size", "data_start", "bss_size")}}
    overrides = values.get("symbols", {})
    if not isinstance(overrides, dict) or overrides.keys() - defaults.keys():
        raise ValueError("invalid native symbol map")
    symbols = {**defaults, **overrides}
    if any(not isinstance(name, str) or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name)
           for name in symbols.values()) or len(set(symbols.values())) != len(symbols):
        raise ValueError("invalid native symbol names")
    return {"board_id": board, **geometry, "permissions": permissions, "symbols": symbols}
