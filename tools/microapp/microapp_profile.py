"""Host interface and ARM target configuration, independent of placement."""
import json
from pathlib import Path

def load_profile(path: Path) -> dict:
    p = json.loads(path.read_text(encoding="utf-8"))
    required = {"board_id", "architecture", "float_abi", "abi_major", "abi_minor",
                "host_version", "max_body_size", "max_ram_size"}
    if not isinstance(p, dict) or required - p.keys():
        raise ValueError("profile requires ARM target, host ABI and capacity limits")
    if p["architecture"] != "armv7e-m" or p["float_abi"] not in ("soft", "softfp", "hard"):
        raise ValueError("only ARMv7E-M with explicit float ABI is supported")
    if not p["board_id"] or len(p["board_id"].encode("ascii")) >= 32:
        raise ValueError("invalid board_id")
    for key in ("abi_major", "abi_minor", "host_version", "max_body_size", "max_ram_size"):
        value = p[key]
        p[key] = int(value, 0) if isinstance(value, str) else value
        if type(p[key]) is not int or not 0 <= p[key] <= 0xffffffff:
            raise ValueError(f"invalid {key}")
    bits = set()
    for name, value in p.get("permissions", {}).items():
        if not name or type(value) is not int or not 0 < value <= 0x80000000 or value & (value-1) or value in bits:
            raise ValueError("permissions require distinct single bits")
        bits.add(value)
    return p
