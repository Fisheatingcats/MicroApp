#!/usr/bin/env python3
"""Pack a fixed-address native app image using an explicit board profile."""

from __future__ import annotations

import argparse
import binascii
import re
import struct
import subprocess
from pathlib import Path
from profile import load_profile
from typing import NamedTuple


PACKAGE_MAGIC = 0x504B5746
FORMAT_MAJOR = 1
FORMAT_MINOR = 0
ABI_MAJOR = 1
ABI_MINOR = 2
PACKAGE_HEADER_SIZE = 56
MANIFEST_SIZE = 280
RESOURCE_TABLE_MAGIC = 0x54525746
RESOURCE_TABLE_VERSION = 1

PERMISSIONS = {
    "GUI": 1 << 0,
    "LOG": 1 << 1,
    "RESOURCE_RO": 1 << 2,
    "MOTOR": 1 << 3,
}

PACKAGE_HEADER_STRUCT = struct.Struct("<IHHIIIIIIIIIIII")
MANIFEST_STRUCT = struct.Struct("<32s32s32s16s32s64s18I")
RESOURCE_TABLE_HEADER_STRUCT = struct.Struct("<IHHI")
RESOURCE_ENTRY_STRUCT = struct.Struct("<32sIII")


class ResourceInput(NamedTuple):
    name: str
    path: Path


def cstr(value: str, size: int) -> bytes:
    raw = value.encode("ascii")
    if len(raw) >= size:
        raise ValueError(f"{value!r} is too long for {size} bytes")
    return raw + b"\0" * (size - len(raw))


def nm_symbols(elf: Path) -> dict[str, int]:
    output = subprocess.check_output(
        ["arm-none-eabi-nm", "-n", str(elf)],
        text=True,
        stderr=subprocess.STDOUT,
    )
    symbols: dict[str, int] = {}
    for line in output.splitlines():
        match = re.match(r"^([0-9a-fA-F]+)\s+\w\s+(\S+)$", line)
        if match:
            symbols[match.group(2)] = int(match.group(1), 16)
    return symbols


def parse_resource(value: str) -> ResourceInput:
    if "=" in value:
        name, path = value.split("=", 1)
    else:
        path = value
        name = Path(value).name
    if not name or len(name.encode("ascii")) >= 32:
        raise argparse.ArgumentTypeError("resource name must be 1..31 ASCII bytes")
    return ResourceInput(name=name, path=Path(path))


def align4(data: bytes) -> bytes:
    return data + (b"\0" * ((4 - (len(data) % 4)) % 4))


def build_resource_blob(resources: list[ResourceInput]) -> bytes:
    if not resources:
        return b""
    if len(resources) > 0xFFFF:
        raise ValueError("too many resources")
    entry_count = len(resources)
    table_size = RESOURCE_TABLE_HEADER_STRUCT.size + entry_count * RESOURCE_ENTRY_STRUCT.size
    payload = bytearray()
    entries = bytearray()

    for resource in resources:
        raw = resource.path.read_bytes()
        offset = table_size + len(payload)
        entries.extend(
            RESOURCE_ENTRY_STRUCT.pack(
                cstr(resource.name, 32),
                offset,
                len(raw),
                binascii.crc32(raw) & 0xFFFFFFFF,
            )
        )
        payload.extend(align4(raw))

    return (
        RESOURCE_TABLE_HEADER_STRUCT.pack(
            RESOURCE_TABLE_MAGIC,
            RESOURCE_TABLE_VERSION,
            entry_count,
            table_size,
        )
        + bytes(entries)
        + bytes(payload)
    )


def required_symbol(symbols: dict[str, int], name: str) -> int:
    if name not in symbols:
        raise SystemExit(f"{name} symbol not found")
    return symbols[name]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", required=True, type=Path)
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--app-id", required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--author", default="MicroApp")
    parser.add_argument("--description", default="Native .fwapp")
    parser.add_argument("--permission", action="append", default=[])
    parser.add_argument("--resource", action="append", default=[],
                        type=parse_resource,
                        help="Add read-only resource as NAME=PATH")
    parser.add_argument("--profile", required=True, type=Path, help="Board geometry JSON")
    args = parser.parse_args()
    profile = load_profile(args.profile)

    image = args.image.read_bytes()
    if not image:
        raise SystemExit("image is empty")
    if len(image) > profile["slot_size"] - profile["image_offset"]:
        raise SystemExit("image exceeds app slot")

    symbols = nm_symbols(args.elf)
    entry_addr = required_symbol(symbols, "fwapp_main")
    text_start = required_symbol(symbols, "__fwapp_text_start")
    text_size = required_symbol(symbols, "__fwapp_text_size")
    data_load = required_symbol(symbols, "__fwapp_data_load")
    data_size = required_symbol(symbols, "__fwapp_data_size")
    data_start = required_symbol(symbols, "__fwapp_data_start")
    bss_size = required_symbol(symbols, "__fwapp_bss_size")
    entry_addr &= ~1
    slot_image_addr = profile["slot_base"] + profile["image_offset"]
    if not (slot_image_addr <= entry_addr < slot_image_addr + len(image)):
        raise SystemExit("fwapp_main is outside app image")
    if text_start != slot_image_addr:
        raise SystemExit("text section must start at app image base")
    if data_size > profile["ram_size"] or bss_size > profile["ram_size"] - data_size:
        raise SystemExit("APP_RAM overflow")
    if data_size and not (slot_image_addr <= data_load < slot_image_addr + len(image)):
        raise SystemExit("data load address is outside app image")
    if data_start != profile["ram_base"]:
        raise SystemExit("data section must start at APP_RAM base")

    permission_mask = 0
    for permission in args.permission:
        if permission not in PERMISSIONS:
            raise SystemExit(f"unknown permission {permission}")
        permission_mask |= PERMISSIONS[permission]

    image_crc = binascii.crc32(image) & 0xFFFFFFFF
    entry_offset = entry_addr - profile["slot_base"]
    resource_blob = build_resource_blob(args.resource)
    installed_resource_offset = profile["image_offset"] + len(image)
    installed_resource_offset = (installed_resource_offset + 3) & ~3
    if resource_blob and installed_resource_offset + len(resource_blob) > profile["slot_size"]:
        raise SystemExit("image plus resources exceed app slot")
    resource_crc = binascii.crc32(resource_blob) & 0xFFFFFFFF if resource_blob else 0

    data_offset = (data_load - profile["slot_base"]) if data_size else 0
    manifest = MANIFEST_STRUCT.pack(
        cstr(profile["board_id"], 32),
        cstr(args.app_id, 32),
        cstr(args.name, 32),
        cstr(args.version, 16),
        cstr(args.author, 32),
        cstr(args.description, 64),
        ABI_MAJOR,
        ABI_MINOR,
        1,
        permission_mask,
        profile["slot_base"],
        profile["slot_size"],
        profile["image_offset"],
        len(image),
        image_crc,
        entry_offset,
        profile["ram_base"],
        profile["ram_size"],
        data_offset,
        data_size,
        bss_size,
        installed_resource_offset if resource_blob else 0,
        len(resource_blob),
        resource_crc,
    )

    header_size = PACKAGE_HEADER_STRUCT.size
    if header_size != PACKAGE_HEADER_SIZE or len(manifest) != MANIFEST_SIZE:
        raise SystemExit("tool/package struct size mismatch")
    manifest_offset = header_size
    image_offset = manifest_offset + len(manifest)
    resource_offset = image_offset + len(image)
    resource_offset = (resource_offset + 3) & ~3
    signature_offset = resource_offset + len(resource_blob)
    signature_size = 0
    signed_size = signature_offset
    total_size = signature_offset + signature_size

    header_without_crc = PACKAGE_HEADER_STRUCT.pack(
        PACKAGE_MAGIC,
        FORMAT_MAJOR,
        FORMAT_MINOR,
        header_size,
        total_size,
        manifest_offset,
        len(manifest),
        image_offset,
        len(image),
        resource_offset if resource_blob else 0,
        len(resource_blob),
        signature_offset,
        signature_size,
        signed_size,
        0,
    )
    package_prefix = header_without_crc + manifest + image
    package_prefix = package_prefix + (b"\0" * (resource_offset - len(package_prefix)))
    payload_for_crc = package_prefix + resource_blob
    crc = binascii.crc32(payload_for_crc[:signed_size]) & 0xFFFFFFFF
    header = PACKAGE_HEADER_STRUCT.pack(
        PACKAGE_MAGIC,
        FORMAT_MAJOR,
        FORMAT_MINOR,
        header_size,
        total_size,
        manifest_offset,
        len(manifest),
        image_offset,
        len(image),
        resource_offset if resource_blob else 0,
        len(resource_blob),
        signature_offset,
        signature_size,
        signed_size,
        crc,
    )

    args.out.parent.mkdir(parents=True, exist_ok=True)
    package = header + manifest + image
    package = package + (b"\0" * (resource_offset - len(package)))
    args.out.write_bytes(package + resource_blob)
    print(f"wrote {args.out} ({total_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
