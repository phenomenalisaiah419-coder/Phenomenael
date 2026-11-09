#!/usr/bin/env python3
"""Network-free security tests for the Android login-cache export helper."""
from __future__ import annotations

import importlib.util
import json
import struct
import tempfile
import unittest
import zipfile
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "export_android_cache.py"
SPEC = importlib.util.spec_from_file_location("bedrock_android_cache_export", SCRIPT)
assert SPEC and SPEC.loader
exporter = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(exporter)


class CacheExportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.cache = self.root / "cache"
        self.cache.mkdir()
        # The framing shape is enough for export validation; the native cache provider
        # performs the stronger key-pair and Login-payload checks before opening a socket.
        self.payload = struct.pack(">I", 2193) + bytes([3]) + b"abc"
        (self.cache / "login_payload.bin").write_bytes(self.payload)
        (self.cache / "ecdsa_p384.pem").write_text(
            "-----BEGIN PRIVATE KEY-----\nZHVtbXk=\n-----END PRIVATE KEY-----\n", encoding="ascii")
        (self.cache / "profile.json").write_text(json.dumps({
            "game_version": "1.26.50", "protocol": 2193,
            "server_address": "example.org:19132",
            "identity_public_key_spki_b64": "cHVibGlj",
        }), encoding="utf-8")

    def tearDown(self):
        self.temp.cleanup()

    def test_archive_contains_only_three_required_private_files(self):
        (self.cache / "refresh_token.txt").write_text("must-not-export", encoding="utf-8")
        output = self.root / "import.zip"
        exporter.export_cache(self.cache, output)
        with zipfile.ZipFile(output) as archive:
            self.assertEqual(set(archive.namelist()), set(exporter.FILE_LIMITS))
            self.assertEqual(archive.read("login_payload.bin"), self.payload)
            self.assertNotIn(b"must-not-export", b"".join(archive.read(n) for n in archive.namelist()))
        self.assertEqual(output.stat().st_mode & 0o777, 0o600)

    def test_mismatched_protocol_is_rejected(self):
        metadata = json.loads((self.cache / "profile.json").read_text(encoding="utf-8"))
        metadata["protocol"] = 2192
        (self.cache / "profile.json").write_text(json.dumps(metadata), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "protocol 2193"):
            exporter.export_cache(self.cache, self.root / "bad.zip")

    def test_symlink_is_rejected(self):
        (self.cache / "login_payload.bin").unlink()
        outside = self.root / "payload.bin"
        outside.write_bytes(self.payload)
        (self.cache / "login_payload.bin").symlink_to(outside)
        with self.assertRaisesRegex(ValueError, "regular file, not a symlink"):
            exporter.export_cache(self.cache, self.root / "bad.zip")


if __name__ == "__main__":
    unittest.main(verbosity=2)
