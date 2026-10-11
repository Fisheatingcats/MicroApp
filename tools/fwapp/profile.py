"""Explicit board geometry for the existing fixed-address native package format."""
from __future__ import annotations

import json
from pathlib import Path


def load_profile(path: Path) -> dict[str, int | str]:
    values = json.loads(path.read_text(encoding="utf-8"))
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
    return {"board_id": board, **geometry}
