#!/usr/bin/env python3
"""Export only the target-bound Bedrock Login material for Android app-private import."""
from __future__ import annotations

import argparse
import json
import os
import stat
import struct
import tempfile
import zipfile
from pathlib import Path

GAME_VERSION = "1.26.50"
PROTOCOL = 2193
FILE_LIMITS = {
    "login_payload.bin": 1024 * 1024,
    "ecdsa_p384.pem": 32 * 1024,
    "profile.json": 64 * 1024,
}


def validate_cache(cache_dir: Path) -> dict:
    if not cache_dir.is_dir():
        raise ValueError(f"cache directory does not exist: {cache_dir}")
    for name, limit in FILE_LIMITS.items():
        path = cache_dir / name
        try:
            info = path.lstat()
        except FileNotFoundError as exc:
            raise ValueError(f"required cache file is missing: {name}") from exc
        if not stat.S_ISREG(info.st_mode):
            raise ValueError(f"required cache path must be a regular file, not a symlink: {name}")
        if info.st_size <= 0 or info.st_size > limit:
            raise ValueError(f"{name} is empty or exceeds its {limit}-byte limit")

    profile = json.loads((cache_dir / "profile.json").read_text(encoding="utf-8"))
    if not isinstance(profile, dict):
        raise ValueError("profile.json must contain a JSON object")
    if profile.get("game_version") != GAME_VERSION or profile.get("protocol") != PROTOCOL:
        raise ValueError("cache is not bound to Bedrock 1.26.50 / protocol 2193")
    if not isinstance(profile.get("server_address"), str) or not profile["server_address"].strip():
        raise ValueError("profile.json has no target server_address")
    if not isinstance(profile.get("identity_public_key_spki_b64"), str):
        raise ValueError("profile.json has no identity public key metadata")

    key = (cache_dir / "ecdsa_p384.pem").read_bytes()
    if not key.startswith(b"-----BEGIN PRIVATE KEY-----") or b"-----END PRIVATE KEY-----" not in key:
        raise ValueError("ecdsa_p384.pem is not an unencrypted PKCS#8 PRIVATE KEY PEM")
    payload = (cache_dir / "login_payload.bin").read_bytes()
    if len(payload) < 6 or struct.unpack(">I", payload[:4])[0] != PROTOCOL:
        raise ValueError("login_payload.bin has an invalid Bedrock protocol header")
    length = 0
    shift = 0
    pos = 4
    for _ in range(5):
        if pos >= len(payload):
            raise ValueError("login_payload.bin has a truncated length prefix")
        byte = payload[pos]
        pos += 1
        length |= (byte & 0x7F) << shift
        if (byte & 0x80) == 0:
            break
        shift += 7
    else:
        raise ValueError("login_payload.bin has an invalid length prefix")
    if pos + length != len(payload):
        raise ValueError("login_payload.bin length does not match its frame envelope")
    return profile


def export_cache(cache_dir: Path, output: Path) -> dict:
    cache_dir = cache_dir.expanduser().resolve()
    output = output.expanduser().absolute()
    profile = validate_cache(cache_dir)
    if output.resolve(strict=False) == cache_dir / "login_payload.bin" or output.resolve(strict=False).parent == cache_dir:
        raise ValueError("output archive must be outside the source cache directory")
    output.parent.mkdir(parents=True, exist_ok=True)

    fd, temp_name = tempfile.mkstemp(prefix=".bedrock-cache-", suffix=".zip.tmp", dir=output.parent)
    os.close(fd)
    temp = Path(temp_name)
    try:
        with zipfile.ZipFile(temp, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for name in FILE_LIMITS:
                info = zipfile.ZipInfo(name)
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = (stat.S_IFREG | 0o600) << 16
                archive.writestr(info, (cache_dir / name).read_bytes())
        os.chmod(temp, 0o600)
        os.replace(temp, output)
    except Exception:
        try:
            temp.unlink()
        except FileNotFoundError:
            pass
        raise
    return profile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output_zip", type=Path, help="destination ZIP archive (contains credentials; protect it)")
    parser.add_argument("--cache-dir", type=Path, default=Path.home() / ".bedrock_ai",
                        help="source cache directory (default: ~/.bedrock_ai)")
    args = parser.parse_args()
    try:
        profile = export_cache(args.cache_dir, args.output_zip)
    except (OSError, ValueError, json.JSONDecodeError, zipfile.BadZipFile) as exc:
        parser.error(str(exc))
    print("Android cache archive created.")
    print(f"Target: {profile['server_address']}")
    print(f"Output: {args.output_zip.expanduser().absolute()}")
    print("Included only: login_payload.bin, ecdsa_p384.pem, profile.json")
    print("WARNING: this archive contains a private identity key and a ready-to-use Login payload. Import it only into the intended app; do not post or share it.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
