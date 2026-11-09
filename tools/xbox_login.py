#!/usr/bin/env python3
"""Termux-friendly Microsoft/Xbox login and Bedrock 1.26.50 cache builder.

Interactive sign-in uses Microsoft's device-code flow; this script never asks
for a password. Xbox auth uses the Bedrock Android XASD + SISU flow, then gets
XSTS explicitly scoped to multiplayer.minecraft.net. Credentials are not
printed. The cache is local-only and should never be shared.

Usage: python tools/xbox_login.py <server-host-or-ip> [19132]
Dependencies: requests, cryptography, PyJWT
"""
from __future__ import annotations

import base64
import hashlib
import json
import os
import secrets
import struct
import sys
import time
import uuid
from pathlib import Path
from typing import Any

try:
    import requests
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature
    import jwt as pyjwt
except ImportError as exc:
    print("Missing dependency. Run: python -m pip install requests cryptography PyJWT", file=sys.stderr)
    raise SystemExit(2) from exc

# These values match the current public Bedrock Android XAL configuration.
CLIENT_ID = "0000000048183522"
SCOPE = "service::user.auth.xboxlive.com::MBI_SSL"
LIVE_DEVICE = "https://login.live.com/oauth20_connect.srf"
LIVE_TOKEN = "https://login.live.com/oauth20_token.srf"
DEVICE_AUTH = "https://device.auth.xboxlive.com/device/authenticate"
SISU_AUTHORIZE = "https://sisu.xboxlive.com/authorize"
XSTS_URL = "https://xsts.auth.xboxlive.com/xsts/authorize"
MC_AUTH = "https://multiplayer.minecraft.net/authentication"
RP_XBOX = "http://xboxlive.com"
RP_MULTIPLAYER = "https://multiplayer.minecraft.net/"
USER_AGENT = "XAL Android 2025.04.20250326.000"
GAME_VERSION = "1.26.50"
PROTOCOL = 2193
WINDOWS_EPOCH_100NS = 116444736000000000
DEFAULT_CACHE = Path(os.environ.get("BEDROCK_AI_CACHE", str(Path.home() / ".bedrock_ai")))
HTTP_TIMEOUT = 30


def b64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode("ascii")


def b64std(data: bytes) -> str:
    return base64.b64encode(data).decode("ascii")


def public_key_spki_b64(public_key: Any) -> str:
    der = public_key.public_bytes(
        encoding=serialization.Encoding.DER,
        format=serialization.PublicFormat.SubjectPublicKeyInfo,
    )
    return b64std(der)


def xbox_proof_key(private_key: ec.EllipticCurvePrivateKey) -> dict[str, str]:
    if not isinstance(private_key.curve, ec.SECP256R1):
        raise ValueError("Xbox proof key must be a P-256 key")
    nums = private_key.public_key().public_numbers()
    return {
        "kty": "EC", "alg": "ES256", "crv": "P-256", "use": "sig",
        "x": b64url(nums.x.to_bytes(32, "big")),
        "y": b64url(nums.y.to_bytes(32, "big")),
    }


def xbox_signature_header(
    private_key: ec.EllipticCurvePrivateKey,
    *, method: str, path_and_query: str, body: bytes,
    authorization: str = "", timestamp_100ns: int | None = None,
) -> str:
    """Build XAL's standard signed-request Signature header (P-256 / ES256)."""
    if timestamp_100ns is None:
        timestamp_100ns = time.time_ns() // 100 + WINDOWS_EPOCH_100NS
    signed = (
        struct.pack(">I", 1) + b"\x00" + struct.pack(">Q", timestamp_100ns) + b"\x00"
        + method.encode("ascii") + b"\x00" + path_and_query.encode("utf-8") + b"\x00"
        + authorization.encode("utf-8") + b"\x00" + body + b"\x00"
    )
    der = private_key.sign(signed, ec.ECDSA(hashes.SHA256()))
    r, s = decode_dss_signature(der)
    raw = r.to_bytes(32, "big") + s.to_bytes(32, "big")
    return b64std(struct.pack(">IQ", 1, timestamp_100ns) + raw)


def safe_json(response: requests.Response) -> dict[str, Any]:
    try:
        payload = response.json()
    except ValueError:
        return {}
    return payload if isinstance(payload, dict) else {}


def response_error(stage: str, response: requests.Response) -> RuntimeError:
    data = safe_json(response)
    xerr = data.get("XErr")
    message = data.get("Message") or data.get("message") or data.get("error_description") or data.get("error")
    detail = f"{stage} failed: HTTP {response.status_code}"
    if xerr is not None:
        detail += f", XErr={xerr}"
    if message:
        detail += f", reason={str(message)[:220]}"
    if not xerr and not message and response.status_code in (401, 403):
        detail += "; Xbox rejected the token/claims. Check account Xbox profile/privacy/child-account permissions. No token was logged."
    return RuntimeError(detail)


def device_code_login(cache_dir: Path) -> dict[str, Any]:
    """Use the known-working Live Connect device-code method, refreshing if possible."""
    refresh_path = cache_dir / "refresh_token.txt"
    if refresh_path.exists():
        refresh_token = refresh_path.read_text(encoding="utf-8").strip()
        if refresh_token:
            try:
                refreshed = requests.post(
                    LIVE_TOKEN,
                    data={"grant_type": "refresh_token", "client_id": CLIENT_ID,
                          "refresh_token": refresh_token, "scope": SCOPE},
                    timeout=HTTP_TIMEOUT,
                )
                if refreshed.status_code == 200:
                    print("Microsoft token refreshed.")
                    return safe_json(refreshed)
            except requests.RequestException:
                pass
            # Continue through interactive device-code sign-in below. Do not print refresh token.

    print("\n=== STEP 1/5: Microsoft sign-in ===")
    response = requests.post(
        LIVE_DEVICE,
        data={"client_id": CLIENT_ID, "scope": SCOPE, "response_type": "device_code"},
        timeout=HTTP_TIMEOUT,
    )
    if response.status_code != 200:
        raise response_error("Microsoft device-code request", response)
    data = safe_json(response)
    try:
        user_code = str(data["user_code"])
        device_code = str(data["device_code"])
        interval = max(1, int(data.get("interval", 5)))
        expires = max(1, int(data.get("expires_in", 900)))
    except (KeyError, TypeError, ValueError) as exc:
        raise RuntimeError("Microsoft device-code response was missing required fields") from exc
    verify_url = data.get("verification_uri") or data.get("verify_url") or "https://www.microsoft.com/link"
    print(f"Open: {verify_url}\nEnter code: {user_code}")
    print("Sign in with the same Microsoft account that owns Minecraft, then return here.")

    started = time.monotonic()
    while time.monotonic() - started < expires:
        time.sleep(interval)
        poll = requests.post(
            LIVE_TOKEN,
            data={"grant_type": "urn:ietf:params:oauth:grant-type:device_code",
                  "client_id": CLIENT_ID, "device_code": device_code},
            timeout=HTTP_TIMEOUT,
        )
        if poll.status_code == 200:
            result = safe_json(poll)
            if not result.get("access_token"):
                raise RuntimeError("Microsoft token endpoint returned success without an access token")
            print("Microsoft sign-in succeeded.")
            return result
        err = str(safe_json(poll).get("error", ""))
        if err == "authorization_pending":
            continue
        if err == "slow_down":
            interval += 5
            continue
        if err in ("expired_token", "code_expired"):
            raise RuntimeError("Microsoft sign-in code expired. Run the login command again.")
        if err in ("authorization_declined", "access_denied"):
            raise RuntimeError("Microsoft sign-in was declined or cancelled.")
        raise response_error("Microsoft token polling", poll)
    raise RuntimeError("Microsoft sign-in timed out. Run the login command again.")


def load_or_create_device_key(cache_dir: Path) -> tuple[ec.EllipticCurvePrivateKey, str]:
    key_path = cache_dir / "xbl_device_p256.pem"
    id_path = cache_dir / "xbl_device_id.txt"
    if key_path.exists() and id_path.exists():
        try:
            key = serialization.load_pem_private_key(key_path.read_bytes(), password=None)
            device_id = str(uuid.UUID(id_path.read_text(encoding="utf-8").strip()))
            if isinstance(key, ec.EllipticCurvePrivateKey) and isinstance(key.curve, ec.SECP256R1):
                return key, device_id
        except (ValueError, TypeError, OSError):
            pass
        raise RuntimeError("Cached Xbox device key/ID is invalid. Back up then remove xbl_device_p256.pem and xbl_device_id.txt, and retry.")
    key = ec.generate_private_key(ec.SECP256R1())
    device_id = str(uuid.uuid4())
    pem = key.private_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PrivateFormat.PKCS8,
        encryption_algorithm=serialization.NoEncryption(),
    )
    key_path.write_bytes(pem)
    id_path.write_text(device_id + "\n", encoding="utf-8")
    os.chmod(key_path, 0o600)
    os.chmod(id_path, 0o600)
    return key, device_id


def obtain_device_token(cache_dir: Path, proof_key: ec.EllipticCurvePrivateKey, device_id: str) -> dict[str, Any]:
    token_path = cache_dir / "xbl_device_token.json"
    if token_path.exists():
        try:
            cached = json.loads(token_path.read_text(encoding="utf-8"))
            if (isinstance(cached, dict) and cached.get("Token") and
                    int(cached.get("expires_unix", 0)) > int(time.time()) + 300):
                return cached
        except (ValueError, TypeError, OSError):
            pass

    print("=== STEP 2/5: Xbox device proof ===")
    body = {
        "Properties": {
            "DeviceType": "Android", "Id": "{" + device_id + "}",
            "AuthMethod": "ProofOfPossession", "ProofKey": xbox_proof_key(proof_key),
        },
        "RelyingParty": "http://auth.xboxlive.com", "TokenType": "JWT",
    }
    body_bytes = json.dumps(body, separators=(",", ":")).encode("utf-8")
    headers = {
        "Content-Type": "application/json", "User-Agent": USER_AGENT,
        "x-xbl-contract-version": "1",
        "Signature": xbox_signature_header(proof_key, method="POST", path_and_query="/device/authenticate", body=body_bytes),
    }
    response = requests.post(DEVICE_AUTH, data=body_bytes, headers=headers, timeout=HTTP_TIMEOUT)
    if response.status_code != 200:
        raise response_error("Xbox device authentication", response)
    result = safe_json(response)
    if not result.get("Token"):
        raise RuntimeError("Xbox device authentication returned no device token")
    # Microsoft's NotAfter is an ISO time. Parse it conservatively; fallback to a 20-hour cache.
    expires_unix = int(time.time()) + 20 * 60 * 60
    not_after = result.get("NotAfter")
    if isinstance(not_after, str):
        try:
            from datetime import datetime
            expires_unix = int(datetime.fromisoformat(not_after.replace("Z", "+00:00")).timestamp())
        except (ValueError, OverflowError):
            pass
    cached = dict(result)
    cached["expires_unix"] = expires_unix
    token_path.write_text(json.dumps(cached), encoding="utf-8")
    os.chmod(token_path, 0o600)
    print("Xbox device proof succeeded.")
    return cached


def sisu_authorize(access_token: str, device_token: str, proof_key: ec.EllipticCurvePrivateKey) -> dict[str, Any]:
    print("=== STEP 3/5: Xbox SISU authorization ===")
    body = {
        "AccessToken": "t=" + access_token,
        "AppId": CLIENT_ID,
        "DeviceToken": device_token,
        "ProofKey": xbox_proof_key(proof_key),
        "Sandbox": "RETAIL",
        "SiteName": "user.auth.xboxlive.com",
        "RelyingParty": RP_XBOX,
        "UseModernGamertag": True,
    }
    body_bytes = json.dumps(body, separators=(",", ":")).encode("utf-8")
    headers = {
        "Content-Type": "application/json", "User-Agent": USER_AGENT,
        "x-xbl-contract-version": "1",
        "Signature": xbox_signature_header(proof_key, method="POST", path_and_query="/authorize", body=body_bytes),
    }
    response = requests.post(SISU_AUTHORIZE, data=body_bytes, headers=headers, timeout=HTTP_TIMEOUT)
    if response.status_code != 200:
        raise response_error("Xbox SISU authorization", response)
    result = safe_json(response)
    required = ("UserToken", "TitleToken", "AuthorizationToken")
    if any(not isinstance(result.get(key), dict) or not result[key].get("Token") for key in required):
        raise RuntimeError("Xbox SISU response is missing UserToken, TitleToken, or AuthorizationToken")
    print("Xbox SISU authorization succeeded.")
    return result


def xsts_for_multiplayer(device_token: str, title_token: str, user_token: str,
                        proof_key: ec.EllipticCurvePrivateKey) -> dict[str, Any]:
    print("=== STEP 4/5: Bedrock multiplayer XSTS ===")
    body = {
        "Properties": {
            "SandboxId": "RETAIL", "DeviceToken": device_token,
            "TitleToken": title_token, "UserTokens": [user_token],
        },
        "RelyingParty": RP_MULTIPLAYER, "TokenType": "JWT",
    }
    body_bytes = json.dumps(body, separators=(",", ":")).encode("utf-8")
    headers = {
        "Content-Type": "application/json", "User-Agent": USER_AGENT,
        "x-xbl-contract-version": "1",
        "Signature": xbox_signature_header(proof_key, method="POST", path_and_query="/xsts/authorize", body=body_bytes),
    }
    response = requests.post(XSTS_URL, data=body_bytes, headers=headers, timeout=HTTP_TIMEOUT)
    if response.status_code != 200:
        raise response_error("Bedrock multiplayer XSTS", response)
    result = safe_json(response)
    claims = result.get("DisplayClaims", {}).get("xui", []) if isinstance(result.get("DisplayClaims"), dict) else []
    if not result.get("Token") or not claims or not isinstance(claims[0], dict) or not claims[0].get("uhs"):
        raise RuntimeError("Bedrock multiplayer XSTS response is missing Token or userhash claim")
    print("Bedrock multiplayer XSTS succeeded.")
    return result


def minecraft_chain(xsts: dict[str, Any], private_key: ec.EllipticCurvePrivateKey) -> tuple[str, dict[str, Any]]:
    print("=== STEP 5/5: Minecraft Bedrock authentication chain ===")
    xui = xsts["DisplayClaims"]["xui"][0]
    userhash = xui["uhs"]
    xsts_value = xsts["Token"]
    identity_public_key = public_key_spki_b64(private_key.public_key())
    body = json.dumps({"identityPublicKey": identity_public_key}, separators=(",", ":")).encode("utf-8")
    response = requests.post(
        MC_AUTH,
        headers={
            "Content-Type": "application/json", "Accept": "application/json",
            "User-Agent": "MCPE/Android", "Client-Version": GAME_VERSION,
            "Authorization": f"XBL3.0 x={userhash};{xsts_value}",
        },
        data=body,
        timeout=HTTP_TIMEOUT,
    )
    if response.status_code != 200:
        raise response_error("Minecraft authentication-chain request", response)
    chain_text = response.text
    try:
        chain_obj = json.loads(chain_text)
    except json.JSONDecodeError as exc:
        raise RuntimeError("Minecraft authentication endpoint returned invalid JSON") from exc
    if not isinstance(chain_obj, dict) or not isinstance(chain_obj.get("chain"), list) or not chain_obj["chain"]:
        raise RuntimeError("Minecraft authentication response has no non-empty JWT chain")
    print("Minecraft authentication chain succeeded.")
    return json.dumps(chain_obj, separators=(",", ":")), xui


def sign_client_data(private_key: ec.EllipticCurvePrivateKey, display_name: str, xuid: str,
                     server_address: str, device_id: str) -> str:
    pub_b64 = public_key_spki_b64(private_key.public_key())
    payload = {
        "AnimatedImageData": [], "CapeData": "", "CapeId": "", "CapeImageHeight": 0,
        "CapeImageWidth": 0, "CapeOnClassicSkin": False,
        "ClientRandomId": secrets.randbits(62), "CurrentInputMode": 2, "DefaultInputMode": 2,
        "DeviceId": device_id, "DeviceModel": "Termux", "DeviceOS": 1,
        "GameVersion": GAME_VERSION, "GuiScale": 0, "LanguageCode": "en_US",
        "PersonaPieces": [], "PieceTintColors": [], "PlatformOfflineId": "",
        "PlatformOnlineId": xuid, "PremiumSkin": False, "SelfSignedId": str(uuid.uuid4()),
        "ServerAddress": server_address, "SkinAnimationData": "", "SkinColor": "#0",
        "SkinData": "", "SkinGeometryData": "", "SkinGeometryDataEngineVersion": "",
        "SkinId": str(uuid.uuid4()) + ".Custom", "SkinImageHeight": 0, "SkinImageWidth": 0,
        "SkinResourcePatch": b64std(json.dumps({"geometry": {"default": "geometry.humanoid.custom"}}, separators=(",", ":")).encode("utf-8")),
        "ThirdPartyName": display_name, "ThirdPartyNameOnly": False, "UIProfile": 0,
        "IsEditorMode": False, "TrustedSkin": False, "OverrideSkin": False,
        "CompatibleWithClientSideChunkGen": True,
    }
    return pyjwt.encode(payload, private_key, algorithm="ES384", headers={"x5u": pub_b64, "alg": "ES384"})


def varuint32(value: int) -> bytes:
    if not 0 <= value <= 0xFFFFFFFF:
        raise ValueError("VarUInt32 value is outside range")
    output = bytearray()
    while value >= 0x80:
        output.append((value & 0x7F) | 0x80)
        value >>= 7
    output.append(value)
    return bytes(output)


def build_login_payload(chain_json: str, client_data_jwt: str, protocol: int = PROTOCOL) -> bytes:
    def le_string(value: str) -> bytes:
        encoded = value.encode("utf-8")
        if len(encoded) > 0x7FFFFFFF:
            raise ValueError("Login string is too long")
        return struct.pack("<I", len(encoded)) + encoded
    connection_request = le_string(chain_json) + le_string(client_data_jwt)
    return struct.pack(">i", protocol) + varuint32(len(connection_request)) + connection_request


def write_private(path: Path, data: bytes) -> None:
    path.write_bytes(data)
    try:
        os.chmod(path, 0o600)
    except OSError:
        pass


def main() -> int:
    cache_dir = DEFAULT_CACHE
    cache_dir.mkdir(parents=True, exist_ok=True)
    try:
        os.chmod(cache_dir, 0o700)
    except OSError:
        pass
    target_host = sys.argv[1] if len(sys.argv) >= 2 else input("Minecraft Bedrock host/IP: ").strip()
    target_port_text = sys.argv[2] if len(sys.argv) >= 3 else "19132"
    try:
        target_port = int(target_port_text)
        if not (1 <= target_port <= 65535):
            raise ValueError
    except ValueError as exc:
        raise RuntimeError("server port must be an integer from 1 to 65535") from exc
    if not target_host or any(c.isspace() for c in target_host) or "/" in target_host:
        raise RuntimeError("server host/IP is invalid")
    # Keep the socket hostname unbracketed, but use canonical host:port notation
    # in signed ClientData/cache metadata (IPv6 literals require brackets).
    cache_host = f"[{target_host}]" if ":" in target_host and not target_host.startswith("[") else target_host
    server_address = f"{cache_host}:{target_port}"
    print(f"Preparing credentials for: {server_address}")

    live = device_code_login(cache_dir)
    access_token = str(live.get("access_token", ""))
    if not access_token:
        raise RuntimeError("Microsoft sign-in returned no access token")
    refresh_token = str(live.get("refresh_token", ""))
    if refresh_token:
        write_private(cache_dir / "refresh_token.txt", refresh_token.encode("utf-8"))

    xbox_key, device_id = load_or_create_device_key(cache_dir)
    device = obtain_device_token(cache_dir, xbox_key, device_id)
    sisu = sisu_authorize(access_token, str(device["Token"]), xbox_key)
    xsts = xsts_for_multiplayer(
        str(device["Token"]), str(sisu["TitleToken"]["Token"]),
        str(sisu["UserToken"]["Token"]), xbox_key,
    )

    identity_key = ec.generate_private_key(ec.SECP384R1())
    chain_json, multiplayer_xui = minecraft_chain(xsts, identity_key)
    # Minecraft's multiplayer-scoped XSTS may omit profile-only claims such as
    # the modern gamertag. SISU's default Xbox token usually carries those.
    default_xui_list = sisu.get("AuthorizationToken", {}).get("DisplayClaims", {}).get("xui", [])
    default_xui = default_xui_list[0] if default_xui_list and isinstance(default_xui_list[0], dict) else {}
    xuid = str(multiplayer_xui.get("xid") or default_xui.get("xid") or "0")
    gamer = str(default_xui.get("gtg") or multiplayer_xui.get("gtg") or "Player")
    client_jwt = sign_client_data(identity_key, gamer, xuid, server_address, str(uuid.uuid4()))
    login_payload = build_login_payload(chain_json, client_jwt, PROTOCOL)

    p384_pem = identity_key.private_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PrivateFormat.PKCS8,
        encryption_algorithm=serialization.NoEncryption(),
    )
    public_key_der = identity_key.public_key().public_bytes(
        encoding=serialization.Encoding.DER,
        format=serialization.PublicFormat.SubjectPublicKeyInfo,
    )
    write_private(cache_dir / "ecdsa_p384.pem", p384_pem)
    write_private(cache_dir / "login_payload.bin", login_payload)
    write_private(cache_dir / "chain.json", chain_json.encode("utf-8"))
    public_b64 = b64std(public_key_der)
    metadata = {
        "display_name": gamer,
        "xuid": xuid,
        "uhs": str(xui["uhs"]),
        "game_version": GAME_VERSION,
        "protocol": PROTOCOL,
        "server_address": server_address,
        "login_payload_bytes": len(login_payload),
        "identity_public_key_spki_b64": public_b64,
        "identity_private_pem_sha256": hashlib.sha256(p384_pem).hexdigest(),
        "auth_flow": "XAL Android device auth + SISU + multiplayer XSTS",
        "cache_created_unix": int(time.time()),
    }
    write_private(cache_dir / "profile.json", json.dumps(metadata, indent=2).encode("utf-8"))
    print("\n=== AUTH CACHE READY ===")
    print(f"Account: {gamer} (XUID {xuid})")
    print(f"Cache: {cache_dir}")
    print("Created: login_payload.bin, ecdsa_p384.pem, profile.json")
    print(f"Target-bound cache: {server_address}")
    print("Next: build/run the C++ connect command with this cache configured.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\nCancelled.", file=sys.stderr)
        raise SystemExit(130)
    except requests.RequestException as exc:
        print(f"\nNETWORK ERROR: {type(exc).__name__}: {exc}", file=sys.stderr)
        raise SystemExit(1)
    except Exception as exc:
        print(f"\nERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
