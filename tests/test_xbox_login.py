#!/usr/bin/env python3
"""Network-free contract tests for the Termux Xbox authentication helper."""
from __future__ import annotations

import base64
import importlib.util
import json
import struct
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "xbox_login.py"
SPEC = importlib.util.spec_from_file_location("bedrock_xbox_login", SCRIPT)
assert SPEC and SPEC.loader
xbox = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(xbox)

try:
    from cryptography.hazmat.primitives.asymmetric import ec
except ImportError as exc:  # pragma: no cover - environment dependency
    raise unittest.SkipTest("cryptography package is required for Xbox auth tests") from exc


class FakeResponse:
    def __init__(self, status_code: int, payload: dict):
        self.status_code = status_code
        self._payload = payload
        self.text = json.dumps(payload)

    def json(self):
        return self._payload


class XboxLoginTests(unittest.TestCase):
    def setUp(self):
        self.key = ec.generate_private_key(ec.SECP256R1())

    def test_proof_key_is_p256_jwk(self):
        jwk = xbox.xbox_proof_key(self.key)
        self.assertEqual(jwk["kty"], "EC")
        self.assertEqual(jwk["alg"], "ES256")
        self.assertEqual(jwk["crv"], "P-256")
        self.assertEqual(len(base64.urlsafe_b64decode(jwk["x"] + "==")), 32)
        self.assertEqual(len(base64.urlsafe_b64decode(jwk["y"] + "==")), 32)

    def test_signature_header_layout(self):
        ts = 133700000000000000
        header = xbox.xbox_signature_header(
            self.key, method="POST", path_and_query="/authorize", body=b'{"a":1}',
            authorization="", timestamp_100ns=ts,
        )
        raw = base64.b64decode(header, validate=True)
        self.assertEqual(len(raw), 12 + 64)
        self.assertEqual(struct.unpack(">I", raw[:4])[0], 1)
        self.assertEqual(struct.unpack(">Q", raw[4:12])[0], ts)

    def test_login_payload_wire_envelope(self):
        chain = json.dumps({"chain": ["a.b.c"]}, separators=(",", ":"))
        client = "header.payload.signature"
        data = xbox.build_login_payload(chain, client, 2193)
        self.assertEqual(data[:4], bytes.fromhex("00000891"))
        length = data[4]
        self.assertEqual(length, len(data) - 5)
        pos = 5
        chain_len = struct.unpack("<I", data[pos:pos + 4])[0]
        pos += 4
        self.assertEqual(data[pos:pos + chain_len].decode(), chain)
        pos += chain_len
        client_len = struct.unpack("<I", data[pos:pos + 4])[0]
        pos += 4
        self.assertEqual(data[pos:pos + client_len].decode(), client)
        self.assertEqual(pos + client_len, len(data))

    def test_sisu_default_rp_request_is_signed_and_bedrock_app_scoped(self):
        response = FakeResponse(200, {
            "UserToken": {"Token": "user"},
            "TitleToken": {"Token": "title"},
            "AuthorizationToken": {"Token": "xbox-default"},
        })
        with patch.object(xbox.requests, "post", return_value=response) as post:
            result = xbox.sisu_authorize("msa-access", "device-token", self.key)
        self.assertEqual(result["TitleToken"]["Token"], "title")
        kwargs = post.call_args.kwargs
        body = json.loads(kwargs["data"].decode())
        self.assertEqual(body["AppId"], xbox.CLIENT_ID)
        self.assertEqual(body["RelyingParty"], "http://xboxlive.com")
        self.assertEqual(body["AccessToken"], "t=msa-access")
        self.assertEqual(body["DeviceToken"], "device-token")
        self.assertEqual(body["SiteName"], "user.auth.xboxlive.com")
        self.assertIn("Signature", kwargs["headers"])
        self.assertEqual(kwargs["headers"]["x-xbl-contract-version"], "1")

    def test_xsts_request_is_scoped_to_bedrock_multiplayer(self):
        response = FakeResponse(200, {
            "Token": "multiplayer-xsts",
            "DisplayClaims": {"xui": [{"uhs": "hash", "xid": "123"}]},
        })
        with patch.object(xbox.requests, "post", return_value=response) as post:
            result = xbox.xsts_for_multiplayer("device", "title", "user", self.key)
        self.assertEqual(result["Token"], "multiplayer-xsts")
        args, kwargs = post.call_args
        self.assertEqual(args[0], xbox.XSTS_URL)
        body = json.loads(kwargs["data"].decode())
        self.assertEqual(body["RelyingParty"], "https://multiplayer.minecraft.net/")
        self.assertEqual(body["Properties"]["DeviceToken"], "device")
        self.assertEqual(body["Properties"]["TitleToken"], "title")
        self.assertEqual(body["Properties"]["UserTokens"], ["user"])

    def test_401_diagnostic_names_xerr_without_echoing_token(self):
        response = FakeResponse(401, {"XErr": 2148916233, "Message": "account requires setup"})
        error = xbox.response_error("Bedrock multiplayer XSTS", response)
        self.assertIn("HTTP 401", str(error))
        self.assertIn("XErr=2148916233", str(error))
        self.assertIn("account requires setup", str(error))
        self.assertNotIn("access_token", str(error))

    def test_malformed_sisu_response_has_actionable_error(self):
        response = FakeResponse(200, {"Token": "not-the-sisu-shape"})
        with patch.object(xbox.requests, "post", return_value=response):
            with self.assertRaisesRegex(RuntimeError, "missing UserToken, TitleToken, or AuthorizationToken"):
                xbox.sisu_authorize("msa-access", "device-token", self.key)


if __name__ == "__main__":
    unittest.main(verbosity=2)
