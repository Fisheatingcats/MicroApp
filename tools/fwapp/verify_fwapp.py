#!/usr/bin/env python3
"""Parse and validate a native .fwapp package against a board profile."""

from __future__ import annotations

import argparse
import binascii
import struct
from pathlib import Path
from profile import load_profile


PACKAGE_MAGIC = 0x504B5746
FORMAT_MAJOR = 1
RESOURCE_TABLE_MAGIC = 0x54525746
RESOURCE_TABLE_VERSION = 1
PACKAGE_HEADER_STRUCT = struct.Struct("<IHHIIIIIIIIIIII")
MANIFEST_STRUCT = struct.Struct("<32s32s32s16s32s64s18I")
RESOURCE_TABLE_HEADER_STRUCT = struct.Struct("<IHHI")
RESOURCE_ENTRY_STRUCT = struct.Struct("<32sIII")


def zstr(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("ascii")


def span_valid(offset: int, size: int, total: int) -> bool:
    return 0 <= offset <= total and 0 <= size <= total - offset


def spans_overlap(a_offset: int, a_size: int, b_offset: int, b_size: int) -> bool:
    if a_size == 0 or b_size == 0:
        return False
    return a_offset < b_offset + b_size and b_offset < a_offset + a_size


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("package", type=Path)
    parser.add_argument("--profile", required=True, type=Path, help="Board geometry JSON")
    args = parser.parse_args()
    profile = load_profile(args.profile)

    data = args.package.read_bytes()
    if len(data) < PACKAGE_HEADER_STRUCT.size:
        raise SystemExit("package too small")

    header = PACKAGE_HEADER_STRUCT.unpack_from(data, 0)
    (
        magic,
        major,
        minor,
        header_size,
        total_size,
        manifest_offset,
        manifest_size,
        image_offset,
        image_size,
        resource_offset,
        resource_size,
        signature_offset,
        signature_size,
        signed_size,
        crc,
    ) = header
    if magic != PACKAGE_MAGIC:
        raise SystemExit("bad magic")
    if major != FORMAT_MAJOR:
        raise SystemExit("bad format major")
    if header_size != PACKAGE_HEADER_STRUCT.size:
        raise SystemExit("bad header size")
    if total_size != len(data):
        raise SystemExit("bad total size")
    if signed_size < header_size or signed_size > len(data):
        raise SystemExit("bad signed size")
    if signature_offset != signed_size:
        raise SystemExit("bad signature offset")
    if signature_size != total_size - signed_size:
        raise SystemExit("bad signature size")
    for label, offset, size, limit in (
        ("manifest", manifest_offset, manifest_size, signed_size),
        ("image", image_offset, image_size, signed_size),
        ("resource", resource_offset, resource_size, signed_size),
        ("signature", signature_offset, signature_size, total_size),
    ):
        if not span_valid(offset, size, limit):
            raise SystemExit(f"bad {label} range")
    if manifest_offset < header_size:
        raise SystemExit("manifest overlaps header")
    if image_offset < header_size:
        raise SystemExit("image overlaps header")
    if resource_size and resource_offset < header_size:
        raise SystemExit("resource overlaps header")
    for label, a_off, a_size, b_off, b_size in (
        ("manifest/image", manifest_offset, manifest_size, image_offset, image_size),
        ("manifest/resource", manifest_offset, manifest_size, resource_offset, resource_size),
        ("image/resource", image_offset, image_size, resource_offset, resource_size),
    ):
        if spans_overlap(a_off, a_size, b_off, b_size):
            raise SystemExit(f"{label} ranges overlap")
    zero_crc_header = PACKAGE_HEADER_STRUCT.pack(*header[:-1], 0)
    crc_data = zero_crc_header + data[header_size:signed_size]
    calc_crc = binascii.crc32(crc_data) & 0xFFFFFFFF
    if calc_crc != crc:
        raise SystemExit(f"bad crc: got 0x{calc_crc:08x}, expected 0x{crc:08x}")

    manifest_raw = data[manifest_offset:manifest_offset + manifest_size]
    if len(manifest_raw) != MANIFEST_STRUCT.size:
        raise SystemExit("bad manifest size")
    manifest = MANIFEST_STRUCT.unpack(manifest_raw)
    strings = manifest[:6]
    ints = manifest[6:]
    (
        abi_major,
        abi_minor,
        min_host_version,
        permission_mask,
        slot_base,
        slot_size,
        manifest_image_offset,
        manifest_image_size,
        manifest_image_crc,
        entry_offset,
        ram_base,
        ram_size,
        data_offset,
        data_size,
        bss_size,
        manifest_resource_offset,
        manifest_resource_size,
        manifest_resource_crc,
    ) = ints
    image = data[image_offset:image_offset + image_size]
    if len(image) != image_size:
        raise SystemExit("bad image range")
    image_crc = binascii.crc32(image) & 0xFFFFFFFF
    if image_crc != manifest_image_crc:
        raise SystemExit("bad image crc")
    if zstr(strings[0]) != profile["board_id"]:
        raise SystemExit("bad board id")
    if abi_major != 1:
        raise SystemExit("bad abi major")
    if min_host_version > 1:
        raise SystemExit("host version too old")
    if slot_base != profile["slot_base"] or slot_size != profile["slot_size"]:
        raise SystemExit("bad slot geometry")
    if manifest_image_offset != profile["image_offset"]:
        raise SystemExit("bad manifest image offset")
    if manifest_image_size != image_size:
        raise SystemExit("bad manifest image size")
    if not span_valid(entry_offset, 1, profile["slot_size"]):
        raise SystemExit("bad entry offset")
    if (
        entry_offset < manifest_image_offset
        or entry_offset >= manifest_image_offset + image_size
    ):
        raise SystemExit("entry outside image")
    if ram_base != profile["ram_base"] or ram_size > profile["ram_size"]:
        raise SystemExit("bad ram geometry")
    if data_size > ram_size or bss_size > ram_size - data_size:
        raise SystemExit("bad ram usage")
    if data_size and not span_valid(data_offset, data_size, profile["slot_size"]):
        raise SystemExit("bad data load range")
    if data_size and data_offset < manifest_image_offset:
        raise SystemExit("data load before image")
    if manifest_resource_size != resource_size:
        raise SystemExit("bad resource metadata size")
    if resource_size:
        installed_resource_offset = profile["image_offset"] + image_size
        installed_resource_offset = (installed_resource_offset + 3) & ~3
        if manifest_resource_offset != installed_resource_offset:
            raise SystemExit("bad installed resource offset")
        resource = data[resource_offset:resource_offset + resource_size]
        resource_crc = binascii.crc32(resource) & 0xFFFFFFFF
        if resource_crc != manifest_resource_crc:
            raise SystemExit("bad resource crc")
        if len(resource) < RESOURCE_TABLE_HEADER_STRUCT.size:
            raise SystemExit("bad resource table")
        r_magic, r_ver, r_count, r_table_size = (
            RESOURCE_TABLE_HEADER_STRUCT.unpack_from(resource, 0)
        )
        if r_magic != RESOURCE_TABLE_MAGIC or r_ver != RESOURCE_TABLE_VERSION:
            raise SystemExit("bad resource table header")
        expected_table_size = (
            RESOURCE_TABLE_HEADER_STRUCT.size + r_count * RESOURCE_ENTRY_STRUCT.size
        )
        if r_table_size != expected_table_size or r_table_size > len(resource):
            raise SystemExit("bad resource table size")
        for index in range(r_count):
            entry_offset = (
                RESOURCE_TABLE_HEADER_STRUCT.size + index * RESOURCE_ENTRY_STRUCT.size
            )
            name_raw, res_off, res_size, res_crc = (
                RESOURCE_ENTRY_STRUCT.unpack_from(resource, entry_offset)
            )
            if not zstr(name_raw):
                raise SystemExit("empty resource name")
            if not span_valid(res_off, res_size, len(resource)) or res_off < r_table_size:
                raise SystemExit("bad resource entry range")
            entry_crc = binascii.crc32(resource[res_off:res_off + res_size]) & 0xFFFFFFFF
            if entry_crc != res_crc:
                raise SystemExit("bad resource entry crc")
    elif manifest_resource_offset != 0 or manifest_resource_crc != 0:
        raise SystemExit("bad empty resource metadata")

    print(f"format={major}.{minor}")
    print(f"app_id={zstr(strings[1])}")
    print(f"name={zstr(strings[2])}")
    print(f"version={zstr(strings[3])}")
    print(f"permissions=0x{permission_mask:08x}")
    print(f"image_size={image_size}")
    print(f"data_size={data_size} bss_size={bss_size}")
    print(f"resource_offset={resource_offset} resource_size={resource_size}")
    print(f"resource_crc=0x{manifest_resource_crc:08x}")
    print(f"signature_offset={signature_offset} signature_size={signature_size}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
