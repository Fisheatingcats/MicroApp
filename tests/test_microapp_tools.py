"""Native SDK tool tests without an ARM compiler or host firmware checkout."""
from __future__ import annotations

import contextlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parents[1] / "tools/microapp"
sys.path.insert(0, str(TOOLS))
import pack_microapp
from microapp_profile import load_profile


class NativeToolsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.profile = self.root / "board.json"
        self.geometry = dict(board_id="test_board", slot_base="0x08060000",
                             slot_size="0x20000", image_offset="0x200",
                             ram_base="0x2001e000", ram_size="0x2000",
                             abi_major=7, abi_minor=3, host_version=9, permissions={"DISPLAY": 16})
        self.profile.write_text(json.dumps(self.geometry))
        self.image = self.root / "app.bin"
        self.image.write_bytes(b"\x00\xbf\x70\x47")
        self.resource = self.root / "message.txt"
        self.resource.write_bytes(b"hello")
        self.package = self.root / "app.mapp"
        self.symbols = dict(microapp_main=0x08060201, __microapp_text_start=0x08060200,
                            __microapp_text_size=4, __microapp_data_load=0x08060204,
                            __microapp_data_size=0, __microapp_data_start=0x2001e000,
                            __microapp_bss_size=8)
        argv = ["pack_microapp.py", "--profile", str(self.profile),
                "--image", str(self.image), "--elf", str(self.root / "app.elf"),
                "--out", str(self.package), "--app-id", "test", "--name", "Test",
                "--version", "0.0.1", "--permission", "DISPLAY",
                "--resource", "message=" + str(self.resource)]
        with patch.object(sys, "argv", argv), patch.object(pack_microapp, "nm_symbols", return_value=self.symbols):
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(pack_microapp.main(), 0)

    def verify(self):
        return subprocess.run([sys.executable, str(TOOLS / "verify_microapp.py"),
                               str(self.package), "--profile", str(self.profile)],
                              capture_output=True, text=True)

    def test_roundtrip_with_resource(self):
        result = self.verify()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("app_id=test", result.stdout)
        self.assertIn("format=1.0", result.stdout)
        manifest = pack_microapp.MANIFEST_STRUCT.unpack_from(self.package.read_bytes(), 56)
        self.assertEqual(manifest[6:10], (7, 3, 9, 16))

    def test_corrupted_image(self):
        data = bytearray(self.package.read_bytes())
        data[336] ^= 1
        self.package.write_bytes(data)
        result = self.verify()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("bad crc", result.stderr)

    def test_wrong_board(self):
        self.geometry["board_id"] = "other_board"
        self.profile.write_text(json.dumps(self.geometry))
        result = self.verify()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("bad board id", result.stderr)

    def test_host_abi_and_permissions(self):
        for key, value, error in (("abi_major", 8, "bad abi major"),
                                  ("abi_minor", 2, "bad abi minor"),
                                  ("host_version", 8, "host version too old"),
                                  ("permissions", {"OTHER": 32}, "bad permission mask")):
            with self.subTest(key=key):
                profile = dict(self.geometry)
                profile[key] = value
                self.profile.write_text(json.dumps(profile))
                result = self.verify()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(error, result.stderr)

    def test_profile_contract(self):
        missing = dict(self.geometry)
        del missing["abi_major"]
        self.profile.write_text(json.dumps(missing))
        with self.assertRaises(ValueError):
            load_profile(self.profile)
        for key, value in (("abi_major", -1), ("abi_minor", True),
                           ("host_version", 0x100000000),
                           ("permissions", {"A": 3}),
                           ("permissions", {"A": 1, "B": 1}),
                           ("symbols", {"entry": "bad name"}),
                           ("symbols", {"unexpected": "symbol"})):
            with self.subTest(key=key, value=value):
                profile = dict(self.geometry)
                profile[key] = value
                self.profile.write_text(json.dumps(profile))
                with self.assertRaises(ValueError):
                    load_profile(self.profile)

    def test_profile_bounds(self):
        for key, value in (("slot_base", -1), ("image_offset", 513),
                           ("ram_base", "0xfffffff0")):
            with self.subTest(key=key):
                profile = dict(self.geometry)
                profile[key] = value
                self.profile.write_text(json.dumps(profile))
                with self.assertRaises(ValueError):
                    load_profile(self.profile)


if __name__ == "__main__":
    unittest.main()
